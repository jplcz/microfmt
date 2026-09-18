// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file pl011_sink.hpp @brief ARM PL011 UART controller and sink adapter. */

#include "../microfmt.hpp"
#include "../string_view.hpp"
#include <cstdint>

namespace microfmt {

/**
 * @brief Zero-allocation controller targeting the ARM PrimeCell PL011 UART.
 *
 * Commonly mapped at 0x09000000 on QEMU `virt` and standard ARM development boards.
 * Use `.as_sink()` to obtain a type-erased `microfmt::sink` for formatting pipelines.
 */
class pl011_sink {
public:
  /**
   * @brief Constructs a PL011 UART controller.
   * @param base_addr Memory-mapped base address of the PL011 controller
   * @param translate_crlf Whether to automatically expand `\n` to `\r\n` for terminal compatibility.
   */
  explicit constexpr pl011_sink(uintptr_t base_addr, bool translate_crlf = true) noexcept
      : base_(base_addr), translate_crlf_(translate_crlf) {}

  /**
   * @brief Optional hardware initialization helper.
   */
  void enable() const noexcept {
    volatile uint32_t *cr = reinterpret_cast<volatile uint32_t *>(base_ + 0x30);
    MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;
    // Bit 0: UARTEN (Enabled), Bit 8: TXE (Transmit Enable), Bit 9: RXE (Receive Enable)
    *cr |= (1 << 0) | (1 << 8) | (1 << 9);
    MICROFMT_END_UNSAFE_BUFFER_USAGE;
  }

  /**
   * @brief Returns a type-erased `microfmt::sink` view over this controller instance.
   */
  [[nodiscard]] constexpr sink as_sink() const noexcept {
    return sink{const_cast<void *>(static_cast<const void *>(this)), &pl011_sink::write_impl};
  }

private:
  uintptr_t base_;
  bool translate_crlf_;

  static void write_impl(void *ctx, string_view str) noexcept {
    auto *self = static_cast<pl011_sink *>(ctx);

    volatile uint32_t *dr = reinterpret_cast<volatile uint32_t *>(self->base_);
    volatile uint32_t *fr = reinterpret_cast<volatile uint32_t *>(self->base_ + 0x18); // Flag Register

    MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;
    for (char c : str) {
      if (self->translate_crlf_ && c == '\n') {
        wait_for_fifo_slot(fr);
        *dr = static_cast<uint32_t>('\r');
      }

      wait_for_fifo_slot(fr);
      *dr = static_cast<uint32_t>(c);
    }
    MICROFMT_END_UNSAFE_BUFFER_USAGE;
  }

  static inline void wait_for_fifo_slot(volatile uint32_t *fr_reg) noexcept {
    // Loop while TXFF (Transmit FIFO Full, bit 5) is set
    while ((*fr_reg & (1 << 5)) != 0) {
#if defined(__aarch64__) || defined(__arm__)
      __asm__ volatile("yield" ::: "memory");
#endif
    }
  }
};

} // namespace microfmt