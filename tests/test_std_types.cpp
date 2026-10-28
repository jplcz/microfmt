// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <bitset>
#include <cstddef>
#include <filesystem>

#include <gtest/gtest.h>

#include <microfmt/formatters/std_types.hpp>
#include <microfmt/microfmt.hpp>

TEST(StdTypesTest, FormatsFilesystemPath) {
  microfmt::buffer_sink<64> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", std::filesystem::path("/usr/lib/x.so"));
  EXPECT_EQ(buffer.view(), "/usr/lib/x.so");
}

TEST(StdTypesTest, FormatsBitsetMsbFirst) {
  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", std::bitset<6>(0b100110));
  EXPECT_EQ(buffer.view(), "100110");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{:#}", std::bitset<4>(0b0101));
  EXPECT_EQ(buffer.view(), "0b0101");
}

TEST(StdTypesTest, FormatsByteWithForwardedSpec) {
  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{} {:02x}", std::byte{42}, std::byte{10});
  EXPECT_EQ(buffer.view(), "42 0a");
}

TEST(StdTypesTest, FormatsRatio) {
  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{} {} {}", std::milli{}, std::ratio<4, 2>{}, std::ratio<-3, 4>{});
  EXPECT_EQ(buffer.view(), "1/1000 2 -3/4");
}
