// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <array>
#include <bitset>
#include <cstddef>
#include <functional>
#include <microfmt/formatters/std_debug.hpp>
#include <microfmt/microfmt.hpp>

TEST(StdDebugTest, ArrayPrintsElements) {
  const std::array<int, 3> values{1, 2, 3};

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(values));
  EXPECT_EQ(buffer.view(), "[1, 2, 3]");
}

TEST(StdDebugTest, EmptyArrayPrintsEmptyBrackets) {
  const std::array<int, 0> values{};

  microfmt::buffer_sink<16> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(values));
  EXPECT_EQ(buffer.view(), "[]");
}

TEST(StdDebugTest, BitsetPrintsMsbFirst) {
  const std::bitset<8> bits(0b00000101);

  microfmt::buffer_sink<16> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(bits));
  EXPECT_EQ(buffer.view(), "00000101");
}

TEST(StdDebugTest, ReferenceWrapperForwardsToValue) {
  int value = 42;
  const std::reference_wrapper<int> ref(value);

  microfmt::buffer_sink<16> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(ref));
  EXPECT_EQ(buffer.view(), "42");
}

TEST(StdDebugTest, BytePrintsZeroPaddedHex) {
  const std::byte small_value{0x2};
  const std::byte large_value{0x2a};

  microfmt::buffer_sink<16> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(small_value));
  EXPECT_EQ(buffer.view(), "0x02");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(large_value));
  EXPECT_EQ(buffer.view(), "0x2a");
}
