// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file framed_sink.hpp
 * @brief Wraps a byte stream in checked frames so a core file survives a noisy serial link.
 *
 * Why: a core dump sent over a UART (Universal Asynchronous Receiver-Transmitter)
 * or SEGGER RTT (Real-Time Transfer) channel shares the wire with log text and
 * can lose or corrupt bytes. A raw ELF file cannot tell where the damage is.
 * Framing adds a start marker and a checksum to every piece, so the host can
 * find the dump among other output, detect damage and know when the dump ended.
 *
 * Frame layout (all integers little-endian):
 * @code
 *   0xA5 0x5A | type (1 byte) | length (2 bytes) | payload | CRC-16 (2 bytes)
 * @endcode
 * `type` is 1 for data and 2 for the final frame, whose 4-byte payload is the
 * total number of data bytes sent. The CRC-16 (Cyclic Redundancy Check, variant
 * CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF) covers type, length and
 * payload. `scripts/coredump_unframe.py` turns a captured stream back into a core file.
 *
 * How: write the core through `framed.as_sink()` and call `finish()` last. Data
 * is buffered up to `MaxPayload` bytes per frame in a buffer owned by this object.
 */

#include "../microfmt.hpp"
#include "../reloco.hpp"
#include <cstddef>
#include <cstdint>

namespace microfmt::coredump {

namespace detail {

/** @brief Returns bits 0-7 of @p value as a char. */
constexpr char low_byte(std::uint64_t value) noexcept {
  // NOLINTNEXTLINE(reloco-narrowing-flow): masked to 0..255 and stored bit-for-bit
  return static_cast<char>(static_cast<unsigned char>(value & 0xFFU));
}

/** @brief Continues a CRC-16/CCITT-FALSE computation over @p data. */
constexpr std::uint16_t crc16_update(std::uint16_t crc, string_view data) noexcept {
  for (const char ch : data) {
    std::uint32_t value = crc;
    value ^= static_cast<std::uint32_t>(static_cast<unsigned char>(ch)) << 8U;
    for (int bit = 0; bit < 8; ++bit)
      value = (((value & 0x8000U) != 0U ? ((value << 1U) ^ 0x1021U) : (value << 1U)) & 0xFFFFU);
    crc = static_cast<std::uint16_t>(value & 0xFFFFU);
  }
  return crc;
}

} // namespace detail

inline constexpr std::uint8_t frame_sync0 = 0xA5;
inline constexpr std::uint8_t frame_sync1 = 0x5A;
inline constexpr std::uint8_t frame_type_data = 1;
inline constexpr std::uint8_t frame_type_end = 2;

/** @brief Splits written bytes into CRC-protected frames sent to @p downstream. */
template <std::size_t MaxPayload> class RELOCO_POINTER framed_sink {
  static_assert(MaxPayload > 0 && MaxPayload <= 65535, "MaxPayload must fit the 16-bit length field");

public:
  explicit framed_sink(const sink &downstream) noexcept : downstream_(downstream) {}

  framed_sink(const framed_sink &) = delete;
  framed_sink &operator=(const framed_sink &) = delete;

  [[nodiscard]] sink as_sink() & noexcept RELOCO_LIFETIMEBOUND {
    return sink{this, [](void *ctx, string_view sv) noexcept { static_cast<framed_sink *>(ctx)->write(sv); }};
  }

  /** @brief Appends @p data; full frames are sent immediately. */
  void write(string_view data) noexcept {
    for (const char ch : data) {
      buffer_[used_++] = ch;
      if (used_ == MaxPayload)
        flush();
    }
  }

  /** @brief Sends any buffered bytes as a data frame. */
  void flush() noexcept {
    if (used_ == 0)
      return;
    send_frame(frame_type_data, string_view(buffer_.data(), used_));
    total_ += static_cast<std::uint32_t>(used_ & 0xFFFFU);
    used_ = 0;
  }

  /** @brief Flushes and sends the end frame carrying the total byte count. Call exactly once, last. */
  void finish() noexcept {
    flush();
    microfmt::array<char, 4> count{};
    for (std::size_t i = 0; i < 4; ++i)
      count[i] = detail::low_byte(total_ >> (8U * i));
    send_frame(frame_type_end, string_view(count.data(), count.size()));
  }

  /** @brief Number of payload bytes already sent in data frames. */
  [[nodiscard]] std::uint32_t bytes_sent() const noexcept { return total_; }

private:
  void send_frame(std::uint8_t type, string_view payload) noexcept {
    const std::size_t length = payload.size();
    const microfmt::array<char, 2> sync{detail::low_byte(frame_sync0), detail::low_byte(frame_sync1)};
    const microfmt::array<char, 3> meta{detail::low_byte(type), detail::low_byte(length),
                                        detail::low_byte(length >> 8U)};
    std::uint16_t crc = detail::crc16_update(0xFFFFU, string_view(meta.data(), meta.size()));
    crc = detail::crc16_update(crc, payload);
    const microfmt::array<char, 2> tail{detail::low_byte(crc), detail::low_byte(static_cast<unsigned>(crc) >> 8U)};
    downstream_.write(string_view(sync.data(), sync.size()));
    downstream_.write(string_view(meta.data(), meta.size()));
    downstream_.write(payload);
    downstream_.write(string_view(tail.data(), tail.size()));
  }

  sink downstream_;
  microfmt::array<char, MaxPayload> buffer_{};
  std::size_t used_{0};
  std::uint32_t total_{0};
};

} // namespace microfmt::coredump
