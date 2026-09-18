// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/formatters/repeated_view.hpp>
#include <microfmt/microfmt.hpp>
#include <string>

TEST(RepeatedViewTest, RepeatsIntegerWithoutSeparator) {
  microfmt::buffer_sink<64> output;

  microfmt::format_to(output.as_sink(), "{}", microfmt::repeat(7, 4));

  EXPECT_EQ(output.view(), "7777");
}

TEST(RepeatedViewTest, RepeatsWithSeparator) {
  microfmt::buffer_sink<64> output;

  microfmt::format_to(output.as_sink(), "{}", microfmt::repeat(42, 3, ", "));

  EXPECT_EQ(output.view(), "42, 42, 42");
}

TEST(RepeatedViewTest, HandlesZeroAndOneCounts) {
  microfmt::buffer_sink<64> output;

  microfmt::format_to(output.as_sink(), "{}", microfmt::repeat('x', 0, "-"));
  EXPECT_EQ(output.view(), "");

  output.reset();
  microfmt::format_to(output.as_sink(), "{}", microfmt::repeat('x', 1, "-"));
  EXPECT_EQ(output.view(), "x");
}

TEST(RepeatedViewTest, ForwardsSpecifierToEachRepetition) {
  microfmt::buffer_sink<64> output;

  microfmt::format_to(output.as_sink(), "{:04x}", microfmt::repeat(0x2A, 3, "|"));

  EXPECT_EQ(output.view(), "002a|002a|002a");
}

TEST(RepeatedViewTest, RepeatsStringViewValues) {
  microfmt::buffer_sink<64> output;

  microfmt::format_to(output.as_sink(), "{}", microfmt::repeat(microfmt::string_view("ab"), 3));

  EXPECT_EQ(output.view(), "ababab");
}

TEST(RepeatedViewTest, RepeatsCharacterForPadding) {
  microfmt::buffer_sink<16> output;

  microfmt::format_to(output.as_sink(), "{}", microfmt::repeat('*', 5));

  EXPECT_EQ(output.view(), "*****");
}
