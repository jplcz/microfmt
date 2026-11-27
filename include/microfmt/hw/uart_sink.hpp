// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file uart_sink.hpp
 * @brief Generic polled UART (Universal Asynchronous Receiver-Transmitter) sink.
 *
 * Why: `pl011_sink` only fits one controller. Most chips differ only in how
 * you ask "can I send a byte?" and "send this byte", so this sink takes those
 * two operations as callbacks and works with any UART, including ones behind
 * a vendor HAL (hardware abstraction library).
 *
 * How: pass a context pointer plus a `tx_ready` and a `put_byte` function.
 * Newlines can be expanded to `\r\n` for terminals. If the transmitter stays
 * busy for `max_spins` polls the rest of the write is dropped and counted, so
 * an unplugged cable never hangs the program.
 */

#include "../microfmt.hpp"
#include "../reloco.hpp"
#include <cstddef>
#include <cstdint>

namespace microfmt {

/** @brief Writes formatted output to a UART through user-supplied callbacks. */
class MICROFMT_API_CLASS RELOCO_POINTER uart_sink {
public:
  /** @brief Returns true when the transmitter can accept one more byte. */
  using tx_ready_fn_t = bool (*)(void *ctx) noexcept;
  /** @brief Transmits one byte; only called after `tx_ready` returned true. */
  using put_byte_fn_t = void (*)(void *ctx, std::uint8_t byte) noexcept;

  // NOLINTNEXTLINE(reloco-no-raw-pointer): opaque callback context, same contract as microfmt::sink
  uart_sink(void *ctx, tx_ready_fn_t tx_ready, put_byte_fn_t put_byte, bool translate_crlf = true,
            std::uint32_t max_spins = 100000) noexcept
      : ctx_(ctx), tx_ready_(tx_ready), put_byte_(put_byte), translate_crlf_(translate_crlf), max_spins_(max_spins) {}

  /** @brief Returns a type-erased `microfmt::sink` view over this instance. */
  [[nodiscard]] sink as_sink() & noexcept RELOCO_LIFETIMEBOUND {
    return sink{this, [](void *ctx, string_view sv) noexcept { static_cast<uart_sink *>(ctx)->write(sv); }};
  }

  /** @brief Sends @p data byte by byte; drops the unsent remainder if the transmitter stays busy. */
  void write(string_view data) noexcept {
    for (std::size_t i = 0; i < data.size(); ++i) {
      if (translate_crlf_ && data[i] == '\n' && !send('\r')) {
        dropped_ += data.size() - i;
        return;
      }
      if (!send(data[i])) {
        dropped_ += data.size() - i;
        return;
      }
    }
  }

  /** @brief Number of bytes dropped because the transmitter stayed busy. */
  [[nodiscard]] std::size_t dropped_bytes() const noexcept { return dropped_; }

private:
  bool send(char c) noexcept {
    for (std::uint32_t spin = 0; spin <= max_spins_; ++spin) {
      if (tx_ready_(ctx_)) {
        put_byte_(ctx_, static_cast<std::uint8_t>(c));
        return true;
      }
    }
    return false;
  }

  void *ctx_{nullptr}; // NOLINT(reloco-no-raw-pointer): opaque callback context
  tx_ready_fn_t tx_ready_{nullptr};
  put_byte_fn_t put_byte_{nullptr};
  bool translate_crlf_{true};
  std::uint32_t max_spins_{0};
  std::size_t dropped_{0};
};

} // namespace microfmt
