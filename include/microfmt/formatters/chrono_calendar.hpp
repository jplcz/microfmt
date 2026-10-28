// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file chrono_calendar.hpp
 * @brief C++20 `std::chrono` calendar formatters: `year`, `month`, `day`,
 * `weekday`, `year_month_day` and `hh_mm_ss`.
 *
 * The header compiles to nothing when the standard library lacks the C++20
 * calendar types. See `docs/formatters/time-units-and-location.md`.
 */

#include "../microfmt.hpp"
#include <chrono>
#include <cstdint>

#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
#define MICROFMT_HAS_CHRONO_CALENDAR 1
#else
#define MICROFMT_HAS_CHRONO_CALENDAR 0
#endif

#if MICROFMT_HAS_CHRONO_CALENDAR

namespace microfmt {

namespace detail {
inline void put_padded(const sink &out, uint64_t value, int width) noexcept {
  format_unsigned<radix::decimal>(out, value, false, width);
}
} // namespace detail

/** `std::chrono::year` as a zero-padded 4-digit year (`2026`). */
template <> struct formatter<std::chrono::year> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(std::chrono::year y, const sink &out) const noexcept {
    const int value = static_cast<int>(y);
    if (value < 0) {
      out.put('-');
    }
    detail::put_padded(out, static_cast<uint64_t>(value < 0 ? -value : value), 4);
  }
};

/** `std::chrono::month` as a 2-digit number (`03`); `{:s}` prints `Mar`. */
template <> struct formatter<std::chrono::month> {
  bool as_name{false};
  constexpr void parse(format_parse_context &ctx) noexcept { as_name = ctx.spec() == microfmt::string_view("s"); }
  void format(std::chrono::month m, const sink &out) const noexcept {
    static constexpr microfmt::string_view names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const unsigned value = static_cast<unsigned>(m);
    if (as_name && m.ok()) {
      // index is range-checked by m.ok() above
      RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
      out.write(names[value - 1]);
      RELOCO_END_UNSAFE_BUFFER_USAGE;
    } else {
      detail::put_padded(out, value, 2);
    }
  }
};

/** `std::chrono::day` as a 2-digit number (`07`). */
template <> struct formatter<std::chrono::day> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(std::chrono::day d, const sink &out) const noexcept {
    detail::put_padded(out, static_cast<unsigned>(d), 2);
  }
};

/** `std::chrono::weekday` as a three-letter name (`Mon`); invalid values print `?`. */
template <> struct formatter<std::chrono::weekday> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(std::chrono::weekday wd, const sink &out) const noexcept {
    static constexpr microfmt::string_view names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    if (wd.ok()) {
      // index is range-checked by wd.ok() above
      RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
      out.write(names[wd.c_encoding()]);
      RELOCO_END_UNSAFE_BUFFER_USAGE;
    } else {
      out.put('?');
    }
  }
};

/** `std::chrono::year_month_day` as ISO 8601 `YYYY-MM-DD`; invalid dates get ` (invalid)` appended. */
template <> struct formatter<std::chrono::year_month_day> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const std::chrono::year_month_day &ymd, const sink &out) const noexcept {
    formatter<std::chrono::year>{}.format(ymd.year(), out);
    out.put('-');
    formatter<std::chrono::month>{}.format(ymd.month(), out);
    out.put('-');
    formatter<std::chrono::day>{}.format(ymd.day(), out);
    if (!ymd.ok()) {
      out.write(" (invalid)");
    }
  }
};

/** `std::chrono::hh_mm_ss<D>` as `[-]HH:MM:SS[.fraction]`; the fraction width follows the duration precision. */
template <typename Duration> struct formatter<std::chrono::hh_mm_ss<Duration>> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const std::chrono::hh_mm_ss<Duration> &t, const sink &out) const noexcept {
    using hms = std::chrono::hh_mm_ss<Duration>;
    if (t.is_negative()) {
      out.put('-');
    }
    detail::put_padded(out, static_cast<uint64_t>(t.hours().count()), 2);
    out.put(':');
    detail::put_padded(out, static_cast<uint64_t>(t.minutes().count()), 2);
    out.put(':');
    detail::put_padded(out, static_cast<uint64_t>(t.seconds().count()), 2);
    if constexpr (hms::fractional_width > 0) {
      out.put('.');
      detail::put_padded(out, static_cast<uint64_t>(t.subseconds().count()), hms::fractional_width);
    }
  }
};

} // namespace microfmt

#endif // MICROFMT_HAS_CHRONO_CALENDAR
