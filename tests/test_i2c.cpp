#include <gtest/gtest.h>

#include <cstdint>

#include <microfmt/formatters/i2c.hpp>

TEST(I2cFormatterTest, FormatsSevenBitWriteAndMasksAddress) {
  const uint8_t payload[] = {0x75, 0x00};

  const auto formatted =
      microfmt::format<128>("{}", microfmt::i2c_write(uint8_t{0xE8}, payload));

  EXPECT_EQ(formatted.view(), "I2C [0x68] WR (2 B) DATA: 75 00 -> OK");
}

TEST(I2cFormatterTest, FormatsCompactReadWithLowercaseHex) {
  const uint8_t payload[] = {0x04, 0x2A};

  const auto formatted = microfmt::format<128>(
      "{:cx}", microfmt::i2c_read(uint8_t{0x68}, payload));

  EXPECT_EQ(formatted.view(), "0x68:R[04 2a]");
}

TEST(I2cFormatterTest, FormatsTenBitReadFailureAndMasksAddress) {
  const uint8_t payload[] = {0xDE, 0xAD};

  const auto formatted = microfmt::format<128>(
      "{}", microfmt::i2c_10bit(0x06AB, microfmt::i2c_dir::read,
                                 microfmt::span(payload),
                                 microfmt::i2c_status::timeout));

  EXPECT_EQ(formatted.view(),
            "I2C [0x2AB] 10b RD (2 B) DATA: DE AD -> TIMEOUT");
  EXPECT_EQ(microfmt::format<128>(
                "{:c}", microfmt::i2c_10bit(0x06AB, microfmt::i2c_dir::read,
                                              microfmt::span(payload),
                                              microfmt::i2c_status::timeout))
                .view(),
            "0x2AB:R[DE AD]!(TIMEOUT)");
}
