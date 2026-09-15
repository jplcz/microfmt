// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <chrono>
#include <gtest/gtest.h>
#include <microfmt/formatters/chrono.hpp>
#include <microfmt/microfmt.hpp>

TEST(ChronoTest, FormatsDurationsWithSuffixes) {
  microfmt::buffer_sink<32> buffer;

  microfmt::format_to(buffer.as_sink(), "{}", std::chrono::milliseconds{-125});
  EXPECT_EQ(buffer.view(), "-125ms");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{}", std::chrono::minutes{42});
  EXPECT_EQ(buffer.view(), "42min");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{:c}", std::chrono::seconds{12});
  EXPECT_EQ(buffer.view(), "12");
}

TEST(ChronoTest, FormatsSystemClockTimestamps) {
  microfmt::buffer_sink<64> buffer;
  const std::chrono::system_clock::time_point timestamp{
      std::chrono::milliseconds{97445006}};

  microfmt::format_to(buffer.as_sink(), "{}", timestamp);
  EXPECT_EQ(buffer.view(), "1970-01-02T03:04:05.006Z");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{:d}", timestamp);
  EXPECT_EQ(buffer.view(), "1970-01-02");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{:t}", timestamp);
  EXPECT_EQ(buffer.view(), "03:04:05.006Z");
}

TEST(ChronoTest, FormatsSteadyClockUptime) {
  microfmt::buffer_sink<32> buffer;
  const std::chrono::steady_clock::time_point uptime{
      std::chrono::milliseconds{3723004}};

  microfmt::format_to(buffer.as_sink(), "{}", uptime);

  EXPECT_EQ(buffer.view(), "01:02:03.004");
}
