// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <chrono>

#include <gtest/gtest.h>

#include <microfmt/formatters/chrono_calendar.hpp>
#include <microfmt/microfmt.hpp>

#if MICROFMT_HAS_CHRONO_CALENDAR
using namespace std::chrono;

TEST(ChronoCalendarTest, FormatsDatesAndWeekdays) {
  microfmt::buffer_sink<64> buffer;
  microfmt::format_to(buffer.as_sink(), "{} {} {:s} {}", year_month_day{year{2026}/March/7}, weekday{1}, month{3}, day{5});
  EXPECT_EQ(buffer.view(), "2026-03-07 Mon Mar 05");
}

TEST(ChronoCalendarTest, MarksInvalidDate) {
  microfmt::buffer_sink<64> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", year_month_day{year{2026}/February/30});
  EXPECT_EQ(buffer.view(), "2026-02-30 (invalid)");
}

TEST(ChronoCalendarTest, FormatsTimeOfDayWithFraction) {
  microfmt::buffer_sink<64> buffer;
  microfmt::format_to(buffer.as_sink(), "{} {}", hh_mm_ss{3661s}, hh_mm_ss{-(1h + 2s + 250ms)});
  EXPECT_EQ(buffer.view(), "01:01:01 -01:00:02.250");
}
#endif
