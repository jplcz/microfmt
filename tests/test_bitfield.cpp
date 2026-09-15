// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <gtest/gtest.h>
#include <microfmt/formatters/bitfield.hpp>
#include <microfmt/microfmt.hpp>

// ============================================================================
// Synthesize Test Register Types
// ============================================================================

MICROFMT_DEFINE_REGISTER_TYPE(
    TestUartIsr, uint32_t, MICROFMT_BIT_FLAG(0, "PE"),
    MICROFMT_BIT_FLAG(1, "FE"), MICROFMT_BIT_FLAG(3, "ORE"),
    MICROFMT_BIT_FLAG(5, "RXNE"), MICROFMT_BIT_FLAG(7, "TXE"),
    MICROFMT_BIT_VALUE_DEC(0x3u << 10, 10, "DMA_BURST"),
    MICROFMT_BIT_VALUE_HEX(0xFu << 16, 16, "FIFO_CNT"))

MICROFMT_DEFINE_REGISTER_TYPE(TestSpiStatus, uint8_t,
                              MICROFMT_BIT_FLAG(0, "RXNE"),
                              MICROFMT_BIT_FLAG(1, "TXE"),
                              MICROFMT_BIT_FLAG(7, "BSY"))

// ============================================================================
// Bitfield View Ad-Hoc Tests
// ============================================================================

TEST(BitfieldTest, EmptyFieldsList) {
  microfmt::buffer_sink<64> buf;
  const uint32_t val = 0x12345678;

  microfmt::format_to(
      buf.as_sink(), "{}",
      microfmt::bits(val, microfmt::span<const microfmt::bit_field>{}));
  EXPECT_EQ(buf.view(), "0x12345678 [NONE]");
}

TEST(BitfieldTest, FlagsOnlyNoBitsSet) {
  microfmt::buffer_sink<64> buf;
  static constexpr microfmt::bit_field fields[] = {
      MICROFMT_BIT_FLAG(0, "FLAG0"), MICROFMT_BIT_FLAG(1, "FLAG1")};

  microfmt::format_to(buf.as_sink(), "{}",
                      microfmt::bits(0, microfmt::span(fields)));
  EXPECT_EQ(buf.view(), "0x00000000 [NONE]");
}

TEST(BitfieldTest, SingleAndMultipleFlags) {
  microfmt::buffer_sink<128> buf;
  static constexpr microfmt::bit_field fields[] = {
      MICROFMT_BIT_FLAG(0, "READY"), MICROFMT_BIT_FLAG(2, "INT_EN"),
      MICROFMT_BIT_FLAG(5, "BUSY")};

  // Single flag set
  buf.reset();
  microfmt::format_to(buf.as_sink(), "{}",
                      microfmt::bits(1u << 2, microfmt::span(fields)));
  EXPECT_EQ(buf.view(), "0x00000004 [INT_EN]");

  // Multiple flags set
  buf.reset();
  microfmt::format_to(
      buf.as_sink(), "{}",
      microfmt::bits((1u << 0) | (1u << 5), microfmt::span(fields)));
  EXPECT_EQ(buf.view(), "0x00000021 [READY | BUSY]");
}

TEST(BitfieldTest, MultiBitValueFieldsDecimalAndHex) {
  microfmt::buffer_sink<128> buf;
  static constexpr microfmt::bit_field fields[] = {
      MICROFMT_BIT_VALUE_DEC(0x07u << 0, 0, "MODE"),
      MICROFMT_BIT_VALUE_HEX(0xFFu << 8, 8, "CRC")};

  // MODE = 5, CRC = 0xAB (171)
  const uint32_t val = (5u << 0) | (0xABu << 8);

  microfmt::format_to(buf.as_sink(), "{}",
                      microfmt::bits(val, microfmt::span(fields)));
  EXPECT_EQ(buf.view(), "0x0000ab05 [MODE=5 | CRC=0xab]");
}

TEST(BitfieldTest, CustomFormattingOptions) {
  microfmt::buffer_sink<128> buf;
  static constexpr microfmt::bit_field fields[] = {MICROFMT_BIT_FLAG(0, "A"),
                                                   MICROFMT_BIT_FLAG(1, "B"),
                                                   MICROFMT_BIT_FLAG(2, "C")};

  const uint32_t val = (1u << 0) | (1u << 2);

  // Hide raw hex and use custom separator ", "
  microfmt::format_to(buf.as_sink(), "{}",
                      microfmt::bits(val, microfmt::span(fields),
                                     /*show_raw_hex=*/false, ", "));

  EXPECT_EQ(buf.view(), "[A, C]");
}

TEST(BitfieldTest, IgnoreUnmatchedBits) {
  microfmt::buffer_sink<128> buf;
  static constexpr microfmt::bit_field fields[] = {
      MICROFMT_BIT_FLAG(1, "FLAG1")};

  // Bit 0 and Bit 31 are set, but only Bit 1 is tracked
  const uint32_t val = (1u << 0) | (1u << 31);
  microfmt::format_to(buf.as_sink(), "{}",
                      microfmt::bits(val, microfmt::span(fields)));
  EXPECT_EQ(buf.view(), "0x80000001 [NONE]");
}

// ============================================================================
// Synthesized Register Types Tests
// ============================================================================

TEST(BitfieldTest, SynthesizedRegisterActiveFields) {
  microfmt::buffer_sink<128> buf;

  // Set TXE, RXNE, DMA_BURST=2, FIFO_CNT=4
  TestUartIsr isr = (1u << 7) | (1u << 5) | (2u << 10) | (4u << 16);

  microfmt::format_to(buf.as_sink(), "{}", isr);
  EXPECT_EQ(buf.view(), "0x000408a0 [RXNE | TXE | DMA_BURST=2 | FIFO_CNT=0x4]");
}

TEST(BitfieldTest, SynthesizedRegisterErrorState) {
  microfmt::buffer_sink<128> buf;

  // Parity Error (PE) + Overrun Error (ORE)
  TestUartIsr isr = (1u << 0) | (1u << 3);

  microfmt::format_to(buf.as_sink(), "{}", isr);
  EXPECT_EQ(buf.view(), "0x00000009 [PE | ORE]");
}

TEST(BitfieldTest, SynthesizedRegister8BitType) {
  microfmt::buffer_sink<64> buf;

  TestSpiStatus spi = (1u << 1) | (1u << 7); // TXE | BSY

  microfmt::format_to(buf.as_sink(), "{}", spi);
  EXPECT_EQ(buf.view(), "0x00000082 [TXE | BSY]");
}

TEST(BitfieldTest, FormatStringEmbedding) {
  auto res = microfmt::format<128>("ISR: {}, SPI: {}", TestUartIsr{(1u << 5)},
                                   TestSpiStatus{(1u << 0)});

  EXPECT_EQ(res.view(), "ISR: 0x00000020 [RXNE], SPI: 0x00000001 [RXNE]");
}