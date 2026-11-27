// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file chunked_sink.hpp
 * @brief Sink that delivers output in packets of at most `ChunkSize` bytes.
 *
 * Why: packet transports such as USB CDC (Communications Device Class, a
 * virtual serial port) move data in fixed-size packets (commonly 64 bytes)
 * and are most efficient when each call carries a full packet. A format call
 * may produce many tiny writes, so this sink collects them and calls your
 * function once per full chunk.
 *
 * How: give it a context and a function. Full chunks are sent automatically;
 * call `flush()` to send a partial chunk (a short packet also marks the end
 * of a USB transfer). With `flush_on_newline` every `\n` flushes, which suits
 * interactive consoles. The sink never flushes from its destructor, because
 * the transport may already be gone; flush explicitly.
 */

#include "../microfmt.hpp"
#include "../reloco.hpp"
#include <cstddef>

namespace microfmt {

/** @brief Packetises output into chunks of at most @p ChunkSize bytes. */
template <std::size_t ChunkSize> class RELOCO_POINTER chunked_sink {
  static_assert(ChunkSize > 0, "ChunkSize must be at least 1 byte");

public:
  /** @brief Receives one chunk of 1..ChunkSize bytes; the view is valid only during the call. */
  using send_fn_t = void (*)(void *ctx, string_view chunk) noexcept;

  // NOLINTNEXTLINE(reloco-no-raw-pointer): opaque callback context, same contract as microfmt::sink
  chunked_sink(void *ctx, send_fn_t send, bool flush_on_newline = false) noexcept
      : ctx_(ctx), send_(send), flush_on_newline_(flush_on_newline) {}

  chunked_sink(const chunked_sink &) = delete;
  chunked_sink &operator=(const chunked_sink &) = delete;

  /** @brief Returns a type-erased `microfmt::sink` view over this instance. */
  [[nodiscard]] sink as_sink() & noexcept RELOCO_LIFETIMEBOUND {
    return sink{this, [](void *ctx, string_view sv) noexcept { static_cast<chunked_sink *>(ctx)->write(sv); }};
  }

  /** @brief Appends @p data, sending every time a chunk fills (and on newline if configured). */
  void write(string_view data) noexcept {
    for (std::size_t i = 0; i < data.size(); ++i) {
      buffer_[used_++] = data[i];
      if (used_ == ChunkSize || (flush_on_newline_ && data[i] == '\n')) {
        flush();
      }
    }
  }

  /** @brief Sends any buffered bytes as a (possibly short) chunk. */
  void flush() noexcept {
    if (used_ == 0) {
      return;
    }
    const std::size_t n = used_;
    used_ = 0;
    send_(ctx_, string_view(buffer_.data(), n));
  }

  /** @brief Bytes buffered and not yet sent. */
  [[nodiscard]] std::size_t pending() const noexcept { return used_; }

private:
  void *ctx_{nullptr}; // NOLINT(reloco-no-raw-pointer): opaque callback context
  send_fn_t send_{nullptr};
  bool flush_on_newline_{false};
  microfmt::array<char, ChunkSize> buffer_{};
  std::size_t used_{0};
};

} // namespace microfmt
