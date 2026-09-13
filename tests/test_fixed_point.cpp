#include <cstdint>
#include <gtest/gtest.h>
#include <microfmt/formatters/fixed_point.hpp>
#include <microfmt/microfmt.hpp>

TEST(FixedPointTest, BasicPositiveScaling) {
  microfmt::buffer_sink<64> buf;

  // 3295 mV -> 3.295 V (Scale = 1000, Decimals = 3)
  microfmt::format_to(buf.as_sink(), "{}", microfmt::milli(3295));
  EXPECT_EQ(buf.view(), "3.295");

  buf.reset();
  // 24100 mV with 2 decimals -> 24.10 V
  microfmt::format_to(buf.as_sink(), "{}", microfmt::fixed<1000, 2>(24100));
  EXPECT_EQ(buf.view(), "24.10");

  buf.reset();
  // 500 with Scale 100, 2 decimals -> 5.00
  microfmt::format_to(buf.as_sink(), "{}", microfmt::centi(500));
  EXPECT_EQ(buf.view(), "5.00");
}

TEST(FixedPointTest, FractionalPadding) {
  microfmt::buffer_sink<64> buf;

  // 5 mV -> 0.005 V
  microfmt::format_to(buf.as_sink(), "{}", microfmt::milli(5));
  EXPECT_EQ(buf.view(), "0.005");

  buf.reset();
  // 40 uA -> 0.000040 A
  microfmt::format_to(buf.as_sink(), "{}", microfmt::micro(40));
  EXPECT_EQ(buf.view(), "0.000040");
}

TEST(FixedPointTest, NegativeNumbers) {
  microfmt::buffer_sink<64> buf;

  // Standard negative: -1250 mV -> -1.250 V
  microfmt::format_to(buf.as_sink(), "{}", microfmt::milli(-1250));
  EXPECT_EQ(buf.view(), "-1.250");

  buf.reset();
  // Negative sub-zero fraction: -50 mV with Scale 1000 -> -0.05 V
  microfmt::format_to(buf.as_sink(), "{}", microfmt::fixed<1000, 2>(-50));
  EXPECT_EQ(buf.view(), "-0.05");

  buf.reset();
  // Negative integer part with zero fraction: -3000 mV -> -3.00 V
  microfmt::format_to(buf.as_sink(), "{}", microfmt::fixed<1000, 2>(-3000));
  EXPECT_EQ(buf.view(), "-3.00");
}

TEST(FixedPointTest, ZeroValue) {
  microfmt::buffer_sink<64> buf;

  microfmt::format_to(buf.as_sink(), "{}", microfmt::fixed<100, 2>(0));
  EXPECT_EQ(buf.view(), "0.00");

  buf.reset();
  // 0 Decimals (integer truncation view)
  microfmt::format_to(buf.as_sink(), "{}", microfmt::fixed<1000, 0>(4850));
  EXPECT_EQ(buf.view(), "4");
}

TEST(FixedPointTest, EmbeddedInFormatString) {
  auto res = microfmt::format<128>("Supply: {}V | Current: {}A | Temp: {} C",
                                   microfmt::fixed<1000, 2>(3312),
                                   microfmt::micro(850), microfmt::centi(-125));

  EXPECT_EQ(res.view(), "Supply: 3.31V | Current: 0.000850A | Temp: -1.25 C");
}
