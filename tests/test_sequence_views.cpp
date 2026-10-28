// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <reloco/array.hpp>

#include <microfmt/formatters/sequence_views.hpp>
#include <microfmt/microfmt.hpp>

TEST(SequenceViewsTest, EnumeratePrintsIndexAndForwardsSpec) {
  const reloco::array<int, 2> values{10, 255};
  microfmt::buffer_sink<64> buffer;
  microfmt::format_to(buffer.as_sink(), "{:x}", microfmt::enumerate(values));
  EXPECT_EQ(buffer.view(), "0: a, 1: ff");
}

TEST(SequenceViewsTest, ZipStopsAtShorterRange) {
  const reloco::array<int, 3> a{1, 2, 3};
  const reloco::array<int, 2> b{7, 8};
  microfmt::buffer_sink<64> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::zip(a, b));
  EXPECT_EQ(buffer.view(), "(1, 7), (2, 8)");
}

TEST(SequenceViewsTest, EmptyRangesPrintNothing) {
  const reloco::array<int, 0> empty{};
  microfmt::buffer_sink<16> buffer;
  microfmt::format_to(buffer.as_sink(), "[{}][{}]", microfmt::enumerate(empty), microfmt::zip(empty, empty));
  EXPECT_EQ(buffer.view(), "[][]");
}
