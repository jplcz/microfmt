// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <vector>

#include <gtest/gtest.h>

#include <microfmt/formatters/sequence_views.hpp>
#include <microfmt/microfmt.hpp>

TEST(SequenceViewsTest, EnumeratePrintsIndexAndForwardsSpec) {
  const std::vector<int> values{10, 255};
  microfmt::buffer_sink<64> buffer;
  microfmt::format_to(buffer.as_sink(), "{:x}", microfmt::enumerate(values));
  EXPECT_EQ(buffer.view(), "0: a, 1: ff");
}

TEST(SequenceViewsTest, ZipStopsAtShorterRange) {
  const std::vector<int> a{1, 2, 3};
  const std::vector<int> b{7, 8};
  microfmt::buffer_sink<64> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::zip(a, b));
  EXPECT_EQ(buffer.view(), "(1, 7), (2, 8)");
}

TEST(SequenceViewsTest, EmptyRangesPrintNothing) {
  const std::vector<int> empty;
  microfmt::buffer_sink<16> buffer;
  microfmt::format_to(buffer.as_sink(), "[{}][{}]", microfmt::enumerate(empty), microfmt::zip(empty, empty));
  EXPECT_EQ(buffer.view(), "[][]");
}
