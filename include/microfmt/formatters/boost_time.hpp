// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file boost_time.hpp @brief Boost.Chrono and Boost.DateTime formatters. */

#include "../microfmt.hpp"
#include <boost/chrono.hpp>
#include <boost/date_time/gregorian/gregorian_types.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <type_traits>

namespace microfmt {
namespace detail {

template <typename Period>
constexpr microfmt::string_view boost_duration_suffix() noexcept {
  if constexpr (Period::num == 1 && Period::den == 1000000000) {
    return "ns";
  } else if constexpr (Period::num == 1 && Period::den == 1000000) {
    return "us";
  } else if constexpr (Period::num == 1 && Period::den == 1000) {
    return "ms";
  } else if constexpr (Period::num == 1 && Period::den == 1) {
    return "s";
  } else if constexpr (Period::num == 60 && Period::den == 1) {
    return "min";
  } else if constexpr (Period::num == 3600 && Period::den == 1) {
    return "h";
  } else {
    return " [custom]";
  }
}

inline void format_boost_date(const boost::gregorian::date &date,
                              const sink &out) noexcept {
  if (date.is_not_a_date()) {
    out.write("not-a-date");
    return;
  }
  if (date.is_pos_infinity()) {
    out.write("+infinity");
    return;
  }
  if (date.is_neg_infinity()) {
    out.write("-infinity");
    return;
  }
  detail::format_unsigned(out, static_cast<uint64_t>(date.year()), 10, false, 4);
  out.put('-');
  detail::format_unsigned(out, static_cast<uint64_t>(date.month()), 10, false,
                          2);
  out.put('-');
  detail::format_unsigned(out, static_cast<uint64_t>(date.day()), 10, false, 2);
}

inline void format_boost_time_duration(
    const boost::posix_time::time_duration &duration,
    const sink &out) noexcept {
  if (duration.is_special()) {
    if (duration.is_not_a_date_time()) {
      out.write("not-a-date-time");
    } else if (duration.is_pos_infinity()) {
      out.write("+infinity");
    } else {
      out.write("-infinity");
    }
    return;
  }

  if (duration.is_negative()) {
    out.put('-');
  }
  detail::format_unsigned(
      out, static_cast<uint64_t>(duration.hours() < 0 ? -duration.hours()
                                                       : duration.hours()),
      10, false, 2);
  out.put(':');
  detail::format_unsigned(
      out, static_cast<uint64_t>(duration.minutes() < 0 ? -duration.minutes()
                                                         : duration.minutes()),
      10, false, 2);
  out.put(':');
  detail::format_unsigned(
      out, static_cast<uint64_t>(duration.seconds() < 0 ? -duration.seconds()
                                                         : duration.seconds()),
      10, false, 2);
  const auto fractional = duration.fractional_seconds();
  if (fractional != 0) {
    out.put('.');
    detail::format_unsigned(
        out, static_cast<uint64_t>(fractional < 0 ? -fractional : fractional),
        10, false,
        static_cast<unsigned>(boost::posix_time::time_duration::num_fractional_digits()));
  }
}

} // namespace detail

template <typename Rep, typename Period>
struct formatter<boost::chrono::duration<Rep, Period>> {
  bool hide_suffix{false};

  constexpr void parse(format_parse_context &ctx) noexcept {
    for (const char character : ctx.spec()) {
      if (character == 'c' || character == 'C') {
        hide_suffix = true;
      }
    }
  }

  void format(const boost::chrono::duration<Rep, Period> &duration,
              const sink &out) const noexcept {
    formatter<Rep> count_formatter;
    format_parse_context count_context("");
    count_formatter.parse(count_context);
    count_formatter.format(duration.count(), out);
    if (!hide_suffix) {
      out.write(detail::boost_duration_suffix<Period>());
    }
  }
};

template <typename Clock, typename Duration>
struct formatter<boost::chrono::time_point<Clock, Duration>> {
  formatter<Duration> duration_formatter{};

  constexpr void parse(format_parse_context &ctx) noexcept {
    duration_formatter.parse(ctx);
  }

  void format(const boost::chrono::time_point<Clock, Duration> &time_point,
              const sink &out) const noexcept {
    duration_formatter.format(time_point.time_since_epoch(), out);
    out.write(" since epoch");
  }
};

template <> struct formatter<boost::gregorian::date> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const boost::gregorian::date &date,
              const sink &out) const noexcept {
    detail::format_boost_date(date, out);
  }
};

template <> struct formatter<boost::posix_time::time_duration> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const boost::posix_time::time_duration &duration,
              const sink &out) const noexcept {
    detail::format_boost_time_duration(duration, out);
  }
};

template <> struct formatter<boost::posix_time::ptime> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const boost::posix_time::ptime &time,
              const sink &out) const noexcept {
    if (time.is_not_a_date_time()) {
      out.write("not-a-date-time");
      return;
    }
    if (time.is_pos_infinity()) {
      out.write("+infinity");
      return;
    }
    if (time.is_neg_infinity()) {
      out.write("-infinity");
      return;
    }
    detail::format_boost_date(time.date(), out);
    out.put('T');
    detail::format_boost_time_duration(time.time_of_day(), out);
  }
};

} // namespace microfmt
