// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "../microfmt.hpp"
#include "../reloco.hpp"

namespace microfmt::gdb {

/**
 * @brief Zero-allocation GDB Remote Serial Protocol packet builder sink.
 *
 * Automatically frames output as `$data#checksum` into a caller-provided stack buffer.
 */
class RELOCO_POINTER gdb_packet_writer {
public:
  explicit constexpr gdb_packet_writer(
      span<char> buffer RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : m_buf(buffer), m_pos(0), m_checksum(0), m_finalized(false) {
    // Start packet with '$'
    if (m_buf.size() > 0) {
      m_buf[0] = '$';
      m_pos = 1;
    }
  }

  /**
   * @brief Creates a type-erased sink adapter for formatting.
   */
  [[nodiscard]] sink as_sink() noexcept RELOCO_LIFETIMEBOUND {
    return sink{this, [](void *ctx, string_view sv) noexcept {
                  auto *self = static_cast<gdb_packet_writer *>(ctx);
                  if (self->m_finalized)
                    return;

                  for (char c : sv) {
                    if (self->m_pos < self->m_buf.size()) {
                      self->m_buf[self->m_pos++] = c;
                      self->m_checksum += static_cast<uint8_t>(c);
                    }
                  }
                }};
  }

  /**
   * @brief Finalizes the packet by appending `#XX` checksum.
   * @return A string view over the complete framed packet, or empty on overflow.
   */
  [[nodiscard]] string_view finalize() noexcept RELOCO_LIFETIMEBOUND {
    if (m_finalized) {
      return string_view(m_buf.data(), m_total_len);
    }

    // We need 4 bytes for `#XX`
    if (m_pos + 3 > m_buf.size()) {
      return string_view{}; // Overflow guard
    }

    m_buf[m_pos++] = '#';

    // Format 8-bit checksum as 2 hex digits
    m_buf[m_pos++] = microfmt::detail::hex_digits_lower[(m_checksum >> 4) & 0x0F];
    m_buf[m_pos++] = microfmt::detail::hex_digits_lower[m_checksum & 0x0F];

    m_total_len = m_pos;
    m_finalized = true;
    return string_view(m_buf.data(), m_total_len);
  }

  [[nodiscard]] constexpr bool is_finalized() const noexcept { return m_finalized; }

private:
  span<char> m_buf;
  size_t m_pos{0};
  size_t m_total_len{0};
  uint8_t m_checksum{0};
  bool m_finalized{false};
};

/**
 * @brief Stateful streaming decoder for incoming GDB Remote Serial Protocol packets.
 *
 * Designed for byte-by-byte ingestion (e.g., from UART RX ring buffers or interrupts)
 * with zero heap allocation and strict stack safety.
 */
class RELOCO_POINTER gdb_streaming_decoder {
public:
  /**
   * @brief Status returned after feeding a byte into the decoder.
   */
  enum class status : uint8_t {
    in_progress,    // Packet framing or payload is still being received
    ready,          // A complete packet was received and checksum validated successfully
    error_overflow, // Payload exceeded the capacity of the scratch buffer
    error_checksum, // Computed checksum did not match the trailing packet checksum
    error_format    // Malformed packet framing or invalid hex characters
  };

  /**
   * @brief Constructs a streaming decoder over a caller-provided scratch span.
   * @param payload_scratch Caller-owned memory buffer to unescape and store the decoded payload.
   */
  explicit constexpr gdb_streaming_decoder(
      span<char> payload_scratch RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : m_buf(payload_scratch) {
    reset();
  }

  /**
   * @brief Resets the decoder state machine back to waiting for a new packet start ('$').
   */
  constexpr void reset() noexcept {
    m_pos = 0;
    m_running_sum = 0;
    m_expected_checksum = 0;
    m_nibble_count = 0;
    m_escaped = false;
    m_state = state::waiting_for_start;
  }

  /**
   * @brief Feeds a single incoming character into the stateful decoder.
   *
   * @param c Incoming character from serial/UART stream.
   * @return status indicating whether a complete packet is ready or an error occurred.
   */
  constexpr status feed(char c) noexcept {
    switch (m_state) {
    case state::waiting_for_start:
      if (c == '$') {
        m_pos = 0;
        m_running_sum = 0;
        m_escaped = false;
        m_state = state::reading_payload;
      }
      // Discard background noise, ACKs ('+'), or NAKs ('-') outside packets
      return status::in_progress;

    case state::reading_payload:
      if (c == '#') {
        m_state = state::reading_checksum_1;
        m_nibble_count = 0;
        m_expected_checksum = 0;
        return status::in_progress;
      }
      if (c == '$') {
        // Restart packet if a new start marker appears unexpectedly
        reset();
        m_state = state::reading_payload;
        return status::in_progress;
      }

      // GDB checksum accumulates raw transmitted bytes (including escape markers)
      m_running_sum += static_cast<uint8_t>(c);

      if (m_escaped) {
        char decoded = static_cast<char>(c ^ 0x20);
        if (m_pos >= m_buf.size()) {
          m_state = state::waiting_for_start;
          return status::error_overflow;
        }
        m_buf[m_pos++] = decoded;
        m_escaped = false;
      } else if (c == '}') {
        m_escaped = true;
      } else {
        if (m_pos >= m_buf.size()) {
          m_state = state::waiting_for_start;
          return status::error_overflow;
        }
        m_buf[m_pos++] = c;
      }
      return status::in_progress;

    case state::reading_checksum_1:
    case state::reading_checksum_2: {
      uint8_t nibble = 0;
      if (!parse_hex_nibble(c, nibble)) {
        m_state = state::waiting_for_start;
        return status::error_format;
      }
      m_expected_checksum = static_cast<uint8_t>((m_expected_checksum << 4) | nibble);

      if (m_state == state::reading_checksum_1) {
        m_state = state::reading_checksum_2;
      } else {
        // Second checksum nibble received — validate
        m_state = state::waiting_for_start;
        if (m_running_sum != m_expected_checksum) {
          return status::error_checksum;
        }
        return status::ready;
      }
      return status::in_progress;
    }
    }

    return status::in_progress;
  }

  /**
   * @brief Returns a non-owning string view over the fully unescaped packet payload.
   * Valid only when feed() returns status::ready.
   */
  [[nodiscard]] constexpr string_view payload() const noexcept RELOCO_LIFETIMEBOUND {
    return string_view(m_buf.data(), m_pos);
  }

private:
  enum class state : uint8_t { waiting_for_start, reading_payload, reading_checksum_1, reading_checksum_2 };

  span<char> m_buf;
  size_t m_pos{0};
  uint8_t m_running_sum{0};
  uint8_t m_expected_checksum{0};
  uint8_t m_nibble_count{0};
  bool m_escaped{false};
  state m_state{state::waiting_for_start};

  [[nodiscard]] static constexpr bool parse_hex_nibble(char c, uint8_t &out_val) noexcept {
    if (c >= '0' && c <= '9') {
      out_val = static_cast<uint8_t>(c - '0');
      return true;
    }
    if (c >= 'a' && c <= 'f') {
      out_val = static_cast<uint8_t>(c - 'a' + 10);
      return true;
    }
    if (c >= 'A' && c <= 'F') {
      out_val = static_cast<uint8_t>(c - 'A' + 10);
      return true;
    }
    return false;
  }
};

} // namespace microfmt::gdb