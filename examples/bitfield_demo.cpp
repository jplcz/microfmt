// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <cstdint>
#include <cstdio>
#include <microfmt/formatters/bitfield.hpp>
#include <microfmt/microfmt.hpp>
#include <string_view>

// Hardware callback example: streams output directly to stdout/UART
static void terminal_write(void * /*ctx*/, std::string_view sv) noexcept {
  std::fwrite(sv.data(), 1, sv.size(), stdout);
}

// ============================================================================
// Synthesize Strongly-Typed Register Descriptors via Macros
// ============================================================================

// Synthesize a 32-bit UART Interrupt & Status Register (ISR)
MICROFMT_DEFINE_REGISTER_TYPE(
    UartIsr, uint32_t, MICROFMT_BIT_FLAG(0, "PE"), // Parity Error
    MICROFMT_BIT_FLAG(1, "FE"),                    // Framing Error
    MICROFMT_BIT_FLAG(3, "ORE"),                   // Overrun Error
    MICROFMT_BIT_FLAG(5, "RXNE"),                  // RX Buffer Not Empty
    MICROFMT_BIT_FLAG(6, "TC"),                    // Transmission Complete
    MICROFMT_BIT_FLAG(7, "TXE"),                   // TX Buffer Empty
    MICROFMT_BIT_VALUE_DEC(0x3u << 10, 10,
                           "DMA_BURST"), // Bits [11:10] decimal value
    MICROFMT_BIT_VALUE_HEX(0xFu << 16, 16, "FIFO_CNT") // Bits [19:16] hex value
)

// Synthesize an 8-bit SPI Status Register (SR)
MICROFMT_DEFINE_REGISTER_TYPE(SpiStatus, uint8_t,
                              MICROFMT_BIT_FLAG(0,
                                                "RXNE"), // RX Buffer Not Empty
                              MICROFMT_BIT_FLAG(1, "TXE"),    // TX Buffer Empty
                              MICROFMT_BIT_FLAG(4, "CRCERR"), // CRC Error
                              MICROFMT_BIT_FLAG(6, "OVR"),    // Overrun
                              MICROFMT_BIT_FLAG(7, "BSY")     // Busy
)

// ============================================================================
// Main Demo Application
// ============================================================================

int main() {
  microfmt::sink term{nullptr, terminal_write};

  // ------------------------------------------------------------------------
  // Scenario A: First-Class Synthesized Register Types
  // ------------------------------------------------------------------------
  microfmt::format_to(term, "=== 1. Synthesized Register Types ===\n");

  // Normal TX/RX activity with FIFO count = 4, DMA Burst = 2
  UartIsr isr_active = (1u << 7) | (1u << 5) | (2u << 10) | (4u << 16);
  microfmt::format_to(term, "UART Active : {}\n", isr_active);

  // Hardware Error State: Parity + Overrun
  UartIsr isr_err = (1u << 0) | (1u << 3);
  microfmt::format_to(term, "UART Error  : {}\n", isr_err);

  // Idle State (No bits set)
  UartIsr isr_idle = 0;
  microfmt::format_to(term, "UART Idle   : {}\n", isr_idle);

  // SPI Peripheral Busy + Transmitting
  SpiStatus spi_stat = (1u << 7) | (1u << 1);
  microfmt::format_to(term, "SPI Status  : {}\n\n", spi_stat);

  // ------------------------------------------------------------------------
  // Scenario B: Ad-hoc/Manual Bitfield Inspection using bits(...)
  // ------------------------------------------------------------------------
  microfmt::format_to(term, "=== 2. Ad-hoc Bitfield Inspection ===\n");

  static constexpr microfmt::bit_field GPIO_MODER_FIELDS[] = {
      MICROFMT_BIT_VALUE_DEC(0x3u << 0, 0, "PIN0"),
      MICROFMT_BIT_VALUE_DEC(0x3u << 2, 2, "PIN1"),
      MICROFMT_BIT_VALUE_DEC(0x3u << 4, 4, "PIN2"),
      MICROFMT_BIT_FLAG(31, "LOCK")};

  // PIN0 = 1 (Output), PIN1 = 2 (AF), PIN2 = 3 (Analog), LOCK = 1
  const uint32_t gpio_val = (1u << 0) | (2u << 2) | (3u << 4) | (1u << 31);

  // Format with standard raw hex prefix
  microfmt::format_to(
      term, "GPIO_MODER: {}\n",
      microfmt::bits(gpio_val, microfmt::span(GPIO_MODER_FIELDS)));

  // Format without raw hex prefix and with custom separator
  microfmt::format_to(term, "Flags only: {}\n",
                      microfmt::bits(gpio_val,
                                     microfmt::span(GPIO_MODER_FIELDS),
                                     /*show_raw_hex=*/false, ", "));

  // ------------------------------------------------------------------------
  // Scenario C: Formatting directly into a fixed stack buffer
  // ------------------------------------------------------------------------
  microfmt::format_to(term, "\n=== 3. Format into Fixed Stack Buffer ===\n");
  auto log_entry = microfmt::format<128>("ISR Snapshot: {}", isr_active);
  std::fwrite(log_entry.view().data(), 1, log_entry.size(), stdout);
  microfmt::format_to(term, "\n");

  return 0;
}
