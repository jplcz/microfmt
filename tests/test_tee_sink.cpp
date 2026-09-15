// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/microfmt.hpp>
#include <microfmt/sinks/tee_sink.hpp>

TEST(TeeSinkTest, BroadcastsFormattedOutputToEveryTarget) {
  microfmt::buffer_sink<32> first;
  microfmt::buffer_sink<32> second;
  auto tee = microfmt::make_tee(first.as_sink(), second.as_sink());

  microfmt::format_to(tee.as_sink(), "value={}", 42);

  EXPECT_EQ(tee.target_count(), 2U);
  EXPECT_EQ(first.view(), "value=42");
  EXPECT_EQ(second.view(), "value=42");
}

TEST(TeeSinkTest, EnforcesTargetCapacity) {
  microfmt::buffer_sink<16> first;
  microfmt::buffer_sink<16> second;
  microfmt::buffer_sink<16> overflow;
  microfmt::tee_sink<2> tee;

  EXPECT_TRUE(tee.add_target(first.as_sink()));
  EXPECT_TRUE(tee.add_target(second.as_sink()));
  EXPECT_FALSE(tee.add_target(overflow.as_sink()));

  microfmt::format_to(tee.as_sink(), "ok");
  EXPECT_EQ(first.view(), "ok");
  EXPECT_EQ(second.view(), "ok");
  EXPECT_TRUE(overflow.view().empty());
}
