// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <microfmt/formatters/filter_view.hpp>
#include <microfmt/microfmt.hpp>
#include <vector>

TEST(FilterViewTest, FormatsOnlyMatchingRangeElements) {
  microfmt::buffer_sink<128> output;
  const std::array<int, 6> samples{{-4, -1, 0, 7, 12, 15}};

  microfmt::format_to(output.as_sink(), "{}",
                      microfmt::filter(samples, [](int value) noexcept {
                        return value >= 0 && value % 2 == 0;
                      }));

  EXPECT_EQ(output.view(), "[0, 12]");
}

TEST(FilterViewTest, HandlesNoMatchesAndEmptyRanges) {
  microfmt::buffer_sink<64> output;
  const std::vector<int> samples{1, 3, 5};

  microfmt::format_to(output.as_sink(), "{}",
                      microfmt::filter(samples, [](int value) noexcept {
                        return value % 2 == 0;
                      }));
  EXPECT_EQ(output.view(), "[]");

  output.reset();
  const std::array<int, 0> empty{};
  microfmt::format_to(output.as_sink(), "{}",
                      microfmt::filter(empty, [](int) noexcept {
                        return true;
                      }));
  EXPECT_EQ(output.view(), "[]");
}

TEST(FilterViewTest, SupportsRawPointerAndCountRanges) {
  microfmt::buffer_sink<64> output;
  const int16_t readings[] = {1200, 1812, 3305, 5004};

  microfmt::format_to(output.as_sink(), "{}",
                      microfmt::filter(readings, size_t{4},
                                       [](int16_t value) noexcept {
                                         return value >= 3000;
                                       }));

  EXPECT_EQ(output.view(), "[3305, 5004]");

  output.reset();
  microfmt::format_to(
      output.as_sink(), "{}",
      microfmt::filter(static_cast<const int16_t *>(nullptr), size_t{4},
                       [](int16_t) noexcept { return true; }));
  EXPECT_EQ(output.view(), "[]");
}

TEST(FilterViewTest, SupportsDelimitersAndForwardedSpecifiers) {
  microfmt::buffer_sink<128> output;
  const uint16_t registers[] = {0x0001, 0x000A, 0x001F, 0x00B0};

  microfmt::format_to(output.as_sink(), "{:c04X}",
                      microfmt::filter(registers, [](uint16_t value) noexcept {
                        return value >= 0x000A;
                      }));
  EXPECT_EQ(output.view(), "{000A, 001F, 00B0}");

  output.reset();
  microfmt::format_to(output.as_sink(), "{:n02x}",
                      microfmt::filter(registers, [](uint16_t value) noexcept {
                        return value < 0x0010;
                      }));
  EXPECT_EQ(output.view(), "01, 0a");

  output.reset();
  microfmt::format_to(output.as_sink(), "{:b}",
                      microfmt::filter(registers, [](uint16_t value) noexcept {
                        return value == 0x001F;
                      }));
  EXPECT_EQ(output.view(), "[31]");
}

TEST(FilterViewTest, RetainsCapturedPredicateState) {
  microfmt::buffer_sink<64> output;
  const int values[] = {8, 16, 24, 32};
  int threshold = 20;

  microfmt::format_to(output.as_sink(), "{}",
                      microfmt::filter(values, [&threshold](int value) noexcept {
                        return value > threshold;
                      }));

  EXPECT_EQ(output.view(), "[24, 32]");
}
