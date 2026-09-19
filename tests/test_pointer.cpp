// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <microfmt/formatters/pointer.hpp>
#include <microfmt/microfmt.hpp>

namespace {

constexpr uintptr_t high_address = UINT64_C(0x123456789ABCDEF0);

microfmt::string_view native_address() { return sizeof(uintptr_t) == 8 ? "0x123456789abcdef0" : "0x9abcdef0"; }

} // namespace

TEST(PointerTest, FormatsNativeAndCompatibilityWidths) {
  microfmt::buffer_sink<64> output;

  microfmt::format_to(output.as_sink(), "{}", microfmt::raw_ptr(high_address));
  EXPECT_EQ(output.view(), native_address());

  output.reset();
  microfmt::format_to(output.as_sink(), "{}", microfmt::raw_ptr(high_address, microfmt::ptr_width_mode::compat32));
  EXPECT_EQ(output.view(), "0x9abcdef0");

  output.reset();
  microfmt::format_to(output.as_sink(), "{}", microfmt::raw_ptr(high_address, microfmt::ptr_width_mode::compat64));
  EXPECT_EQ(output.view(), "0x123456789abcdef0");

  output.reset();
  microfmt::format_to(output.as_sink(), "{}", microfmt::raw_ptr32(high_address));
  EXPECT_EQ(output.view(), "0x9abcdef0");
}

TEST(PointerTest, SupportsPrefixCaseWidthAndModeSpecifiers) {
  microfmt::buffer_sink<64> output;

  microfmt::format_to(output.as_sink(), "{:P}", microfmt::raw_ptr(uintptr_t{0xAB}));
  EXPECT_EQ(output.view(), sizeof(uintptr_t) == 8 ? "0X00000000000000AB" : "0X000000AB");

  output.reset();
  microfmt::format_to(output.as_sink(), "{:x}", microfmt::raw_ptr(uintptr_t{0xAB}));
  EXPECT_EQ(output.view(), sizeof(uintptr_t) == 8 ? "00000000000000ab" : "000000ab");

  output.reset();
  microfmt::format_to(output.as_sink(), "{:08X}", microfmt::raw_ptr(uintptr_t{0xAB}));
  EXPECT_EQ(output.view(), "000000AB");

  output.reset();
  microfmt::format_to(output.as_sink(), "{:32P}", microfmt::raw_ptr(high_address));
  EXPECT_EQ(output.view(), "0X9ABCDEF0");

  output.reset();
  microfmt::format_to(output.as_sink(), "{:64x}", microfmt::raw_ptr(uintptr_t{0x1234}));
  EXPECT_EQ(output.view(), "0000000000001234");
}

TEST(PointerTest, FormatsTypedPointersAndAllNullRepresentations) {
  microfmt::buffer_sink<64> output;
  int value = 42;

  microfmt::format_to(output.as_sink(), "{}", microfmt::raw_ptr(&value));
  EXPECT_EQ(output.view().substr(0, 2), "0x");
  EXPECT_EQ(output.view().size(), 2U + sizeof(uintptr_t) * 2U);

  output.reset();
  microfmt::format_to(output.as_sink(), "{}", microfmt::raw_ptr(static_cast<int *>(nullptr)));
  EXPECT_EQ(output.view(), "(nil)");

  output.reset();
  microfmt::format_to(output.as_sink(), "{:z}", microfmt::raw_ptr(nullptr));
  EXPECT_EQ(output.view(), "0x0");

  output.reset();
  microfmt::format_to(output.as_sink(), "{:Pz}", microfmt::raw_ptr(uintptr_t{0}));
  EXPECT_EQ(output.view(), "0x0");
}

TEST(PointerTest, FormatsPointerRangesFromBeginEndAndCount) {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  microfmt::buffer_sink<128> output;
  const std::array<int, 3> values{{7, 42, -1}};

  microfmt::format_to(output.as_sink(), "{}", microfmt::raw_range(values.data(), values.data() + values.size()));
  EXPECT_EQ(output.view(), "[7, 42, -1]");

  output.reset();
  microfmt::format_to(output.as_sink(), "{}", microfmt::raw_range(values.data() + 1, size_t{2}));
  EXPECT_EQ(output.view(), "[42, -1]");
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

TEST(PointerTest, FormatsEmptyAndNullPointerRanges) {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  microfmt::buffer_sink<64> output;
  const int values[] = {1, 2};

  const auto empty = microfmt::raw_range(values, values);
  EXPECT_EQ(empty.size(), 0U);
  microfmt::format_to(output.as_sink(), "{}", empty);
  EXPECT_EQ(output.view(), "[]");

  output.reset();
  const auto null_range = microfmt::raw_range(static_cast<const int *>(nullptr), size_t{8});
  EXPECT_EQ(null_range.size(), 0U);
  microfmt::format_to(output.as_sink(), "{}", null_range);
  EXPECT_EQ(output.view(), "[]");

  output.reset();
  const auto reversed = microfmt::raw_range(values + 2, values);
  EXPECT_EQ(reversed.size(), 0U);
  microfmt::format_to(output.as_sink(), "{}", reversed);
  EXPECT_EQ(output.view(), "[]");

  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

TEST(PointerTest, ForwardsRangeSpecifiersAndSelectsDelimiters) {
  microfmt::buffer_sink<128> output;
  const uint16_t registers[] = {0x000A, 0x00B0};

  microfmt::format_to(output.as_sink(), "{:c04X}", microfmt::raw_range(registers, size_t{2}));
  EXPECT_EQ(output.view(), "{000A, 00B0}");

  output.reset();
  microfmt::format_to(output.as_sink(), "{:n02x}", microfmt::raw_range(registers, size_t{2}));
  EXPECT_EQ(output.view(), "0a, b0");

  output.reset();
  microfmt::format_to(output.as_sink(), "{:b}", microfmt::raw_range(registers, size_t{2}));
  EXPECT_EQ(output.view(), "[10, 176]");
}
