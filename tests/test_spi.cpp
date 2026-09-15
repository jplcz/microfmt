// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <cstdint>

#include <microfmt/formatters/spi.hpp>

TEST(SpiFormatterTest, FormatsFullDuplexTransfer) {
  const uint8_t mosi[] = {0x9F, 0x00, 0x00};
  const uint8_t miso[] = {0x00, 0xEF, 0x40};

  const auto formatted = microfmt::format<128>(
      "{}", microfmt::spi_duplex(mosi, miso, 1, microfmt::spi_mode::mode3));

  EXPECT_EQ(formatted.view(),
            "SPI [CS1, Mode 3] (3 B) MOSI: 9F 00 00 | MISO: 00 EF 40 -> OK");
}

TEST(SpiFormatterTest, FormatsCompactLowercaseTransfer) {
  const uint8_t mosi[] = {0x0B, 0x00};
  const uint8_t miso[] = {0xDE, 0xAD};

  const auto formatted =
      microfmt::format<128>("{:cx}", microfmt::spi_duplex(mosi, miso));

  EXPECT_EQ(formatted.view(), "CS0:TX[0b 00]/RX[de ad]");
}

TEST(SpiFormatterTest, FormatsSimplexReadFailure) {
  const uint8_t miso[] = {0x00, 0x00};
  const auto transfer = microfmt::spi_read(
      miso, 2, microfmt::spi_mode::mode1, microfmt::spi_status::crc_err);

  EXPECT_EQ(microfmt::format<128>("{}", transfer).view(),
            "SPI [CS2, Mode 1] (2 B) MISO: 00 00 -> CRC_ERR");
  EXPECT_EQ(microfmt::format<128>("{:c}", transfer).view(),
            "CS2:RX[00 00]!(CRC_ERR)");
}
