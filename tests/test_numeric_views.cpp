// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include <microfmt/formatters/numeric_views.hpp>
#include <microfmt/microfmt.hpp>

TEST(NumericViewsTest, GroupsThousands) {
  microfmt::buffer_sink<64> buffer;
  microfmt::format_to(buffer.as_sink(), "{} {} {} {}", microfmt::thousands(0), microfmt::thousands(999),
                      microfmt::thousands(1000), microfmt::thousands(1234567));
  EXPECT_EQ(buffer.view(), "0 999 1,000 1,234,567");
}

TEST(NumericViewsTest, HandlesNegativeAndExtremes) {
  microfmt::buffer_sink<64> buffer;
  microfmt::format_to(buffer.as_sink(), "{} {}", microfmt::thousands(-1234), microfmt::thousands(std::numeric_limits<int64_t>::min()));
  EXPECT_EQ(buffer.view(), "-1,234 -9,223,372,036,854,775,808");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::thousands(std::numeric_limits<uint64_t>::max()));
  EXPECT_EQ(buffer.view(), "18,446,744,073,709,551,615");
}

TEST(NumericViewsTest, CustomSeparatorAndGroupSize) {
  microfmt::buffer_sink<64> buffer;
  microfmt::format_to(buffer.as_sink(), "{} {}", microfmt::thousands(1234567, '_'), microfmt::thousands(12345678, ' ', 4));
  EXPECT_EQ(buffer.view(), "1_234_567 1234 5678");
}
