// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <microfmt/formatters/units.hpp>
#include <microfmt/microfmt.hpp>

TEST(UnitsTest, FormatsFixedUnitAndForwardsIntegerSpec) {
  microfmt::buffer_sink<32> buffer;

  microfmt::format_to(buffer.as_sink(), "{}", microfmt::with_unit(42, "mA"));
  EXPECT_EQ(buffer.view(), "42 mA");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{:04x}",
                      microfmt::with_unit(42, "mA"));
  EXPECT_EQ(buffer.view(), "002a mA");
}

TEST(UnitsTest, AutomaticallyScalesSiValues) {
  microfmt::buffer_sink<32> buffer;

  microfmt::format_to(buffer.as_sink(), "{}", microfmt::hertz(50000000));
  EXPECT_EQ(buffer.view(), "50.00 MHz");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{:.3}", microfmt::hertz(1250));
  EXPECT_EQ(buffer.view(), "1.250 kHz");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::auto_si(-1500, "V"));
  EXPECT_EQ(buffer.view(), "-1.50 kV");
}

TEST(UnitsTest, AutomaticallyScalesBinaryByteValues) {
  microfmt::buffer_sink<32> buffer;

  microfmt::format_to(buffer.as_sink(), "{}", microfmt::auto_bytes(1048576));
  EXPECT_EQ(buffer.view(), "1.00 MiB");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::auto_bytes(999));
  EXPECT_EQ(buffer.view(), "999 B");
}
