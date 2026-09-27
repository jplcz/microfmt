// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/formatters/debug_float.hpp>
#include <microfmt/microfmt.hpp>
#include <limits>

TEST(DebugFloatTest, WholeNumberGetsTrailingDotZero) {
  const double value = 1.0;

  microfmt::buffer_sink<16> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(value));
  EXPECT_EQ(buffer.view(), "1.0");
}

TEST(DebugFloatTest, FractionalValuePrintsAsIs) {
  const double value = 3.5;

  microfmt::buffer_sink<16> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(value));
  EXPECT_EQ(buffer.view(), "3.5");
}

TEST(DebugFloatTest, NegativeWholeNumberGetsTrailingDotZero) {
  const float value = -2.0f;

  microfmt::buffer_sink<16> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(value));
  EXPECT_EQ(buffer.view(), "-2.0");
}

TEST(DebugFloatTest, LongDoubleWholeNumberGetsTrailingDotZero) {
  const long double value = 7.0L;

  microfmt::buffer_sink<16> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(value));
  EXPECT_EQ(buffer.view(), "7.0");
}

TEST(DebugFloatTest, InfinityAndNanPrintUnmodified) {
  const double inf_value = std::numeric_limits<double>::infinity();
  const double nan_value = std::numeric_limits<double>::quiet_NaN();

  microfmt::buffer_sink<16> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(inf_value));
  EXPECT_EQ(buffer.view(), "inf");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(nan_value));
  EXPECT_EQ(buffer.view(), "nan");
}
