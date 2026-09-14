// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <cstdint>
#include <microfmt/formatters/register.hpp>
#include <microfmt/microfmt.hpp>
#include <microfmt/sinks/stdio.hpp>

// ----------------------------------------------------------------------------
// Static Hardware Descriptors (Stored in Flash / .rodata)
// ----------------------------------------------------------------------------

// STM32 / ARM Cortex USART_CR1 Register
inline constexpr microfmt::reg_descriptor USART_CR1_DESC{
    "USART_CR1",
    4,                                         // 32-bit register
    microfmt::reg_field{"UE", 0, 1, true},     // USART Enable
    microfmt::reg_field{"UESM", 1, 1, true},   // Stop Mode Enable
    microfmt::reg_field{"RE", 2, 1, true},     // Receiver Enable
    microfmt::reg_field{"TE", 3, 1, true},     // Transmitter Enable
    microfmt::reg_field{"IDLEIE", 4, 1, true}, // Idle Line Interrupt Enable
    microfmt::reg_field{"RXNEIE", 5, 1, true}, // RX Not Empty Interrupt Enable
    microfmt::reg_field{"TCIE", 6, 1, true},   // Transmission Complete IE
    microfmt::reg_field{"TXEIE", 7, 1, true},  // TX Empty IE
    microfmt::reg_field{"PS", 9, 1, true},     // Parity Selection (Odd/Even)
    microfmt::reg_field{"PCE", 10, 1, true},   // Parity Control Enable
    microfmt::reg_field{"M0", 12, 1, true},    // Word length bit 0
    microfmt::reg_field{"MME", 13, 1, true},   // Mute Mode Enable
    microfmt::reg_field{"OVER8", 15, 1, true}, // Oversampling 8/16
    microfmt::reg_field{"DEDT", 16, 5,
                        false}, // Driver Enable Deassertion Time (5 bits)
    microfmt::reg_field{"DEAT", 21, 5,
                        false} // Driver Enable Assertion Time (5 bits)
};

// Custom Timer Status Register
inline constexpr microfmt::reg_descriptor TIMER_SR_DESC{
    "TIM_SR",
    2,                                        // 16-bit register
    microfmt::reg_field{"UIF", 0, 1, true},   // Update Interrupt Flag
    microfmt::reg_field{"CC1IF", 1, 1, true}, // Capture/Compare 1 Flag
    microfmt::reg_field{"CC2IF", 2, 1, true}, // Capture/Compare 2 Flag
    microfmt::reg_field{"PRESCALER", 8, 4, false}};

int main() {
  auto out = microfmt::stdout_sink();

  // Simulated register read values from MMIO
  const uint32_t cr1_val = 0x0000002D; // UE=1, RE=1, TE=1, RXNEIE=1
  const uint16_t sr_val = 0x0301;      // UIF=1, PRESCALER=3

  microfmt::println(out, "=== Hardware Peripheral Register Decoder ===");

  // 1. Full detailed decode
  microfmt::println(out, "{}", microfmt::format_reg(cr1_val, USART_CR1_DESC));

  // 2. Short decode (only non-zero fields and active flags)
  microfmt::println(out, "{:s}", microfmt::format_reg(cr1_val, USART_CR1_DESC));

  // 3. 16-bit Timer Register
  microfmt::println(out, "{}", microfmt::format_reg(sr_val, TIMER_SR_DESC));
  microfmt::println(out, "{:s}", microfmt::format_reg(sr_val, TIMER_SR_DESC));

  return 0;
}