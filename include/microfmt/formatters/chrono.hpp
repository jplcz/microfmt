// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

/** @file chrono.hpp @brief std::chrono duration and time-point formatters. */

#include "../microfmt.hpp"
#include <chrono>
#include <cstdint>
#include <ratio>
#include <string_view>
#include <type_traits>

namespace microfmt {

// ============================================================================
// Duration Unit Suffix Helpers
// ============================================================================

namespace detail {

template <typename Period> struct duration_suffix {
  static constexpr std::string_view value = " [custom]";
};

template <> struct duration_suffix<std::nano> {
  static constexpr std::string_view value = "ns";
};
template <> struct duration_suffix<std::micro> {
  static constexpr std::string_view value = "us";
};
template <> struct duration_suffix<std::milli> {
  static constexpr std::string_view value = "ms";
};
template <> struct duration_suffix<std::ratio<1>> {
  static constexpr std::string_view value = "s";
};
template <> struct duration_suffix<std::ratio<60>> {
  static constexpr std::string_view value = "min";
};
template <> struct duration_suffix<std::ratio<3600>> {
  static constexpr std::string_view value = "h";
};
template <> struct duration_suffix<std::ratio<86400>> {
  static constexpr std::string_view value = "d";
};

// Days to civil date conversion (Howard Hinnant algorithm, zero-float /
// integer-only)
struct civil_date {
  int32_t year;
  uint8_t month;
  uint8_t day;
};

inline constexpr civil_date civil_from_days(int32_t z) noexcept {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t doe = static_cast<uint32_t>(z - era * 146097);
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int32_t y = static_cast<int32_t>(yoe) + era * 400;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  const uint8_t d = static_cast<uint8_t>(doy - (153 * mp + 2) / 5 + 1);
  const uint8_t m = static_cast<uint8_t>(mp < 10 ? mp + 3 : mp - 9);
  return {y + (m <= 2 ? 1 : 0), m, d};
}

} // namespace detail

// ============================================================================
// Formatter for std::chrono::duration
// ============================================================================

template <typename Rep, typename Period>
struct formatter<std::chrono::duration<Rep, Period>> {
  bool hide_suffix{false};

  constexpr void parse(format_parse_context &ctx) noexcept {
    for (char c : ctx.spec()) {
      if (c == 'c' ||
          c == 'C') { // Compact/Clean: prints raw count without unit suffix
        hide_suffix = true;
      }
    }
  }

  void format(const std::chrono::duration<Rep, Period> &d,
              const sink &out) const noexcept {
    const auto count = d.count();
    if constexpr (std::is_signed_v<Rep>) {
      if (count < 0) {
        out.put('-');
        detail::format_unsigned(out, static_cast<uint64_t>(-count), 10, false,
                                0);
      } else {
        detail::format_unsigned(out, static_cast<uint64_t>(count), 10, false,
                                0);
      }
    } else {
      detail::format_unsigned(out, static_cast<uint64_t>(count), 10, false, 0);
    }

    if (!hide_suffix) {
      out.write(detail::duration_suffix<Period>::value);
    }
  }
};

// ============================================================================
// Formatter for std::chrono::time_point (System Clock / Calendar UTC)
// ============================================================================

template <typename Duration>
struct formatter<std::chrono::time_point<std::chrono::system_clock, Duration>> {
  bool time_only{false};
  bool date_only{false};

  constexpr void parse(format_parse_context &ctx) noexcept {
    for (char c : ctx.spec()) {
      if (c == 'T' || c == 't')
        time_only = true; // {:t} -> "HH:MM:SS"
      if (c == 'D' || c == 'd')
        date_only = true; // {:d} -> "YYYY-MM-DD"
    }
  }

  void
  format(const std::chrono::time_point<std::chrono::system_clock, Duration> &tp,
         const sink &out) const noexcept {
    using namespace std::chrono;
    const auto dur = tp.time_since_epoch();
    const auto total_secs = duration_cast<seconds>(dur).count();
    const auto sub_ms = duration_cast<milliseconds>(dur).count() % 1000;

    const int32_t days = static_cast<int32_t>(total_secs / 86400);
    int32_t rem_secs = static_cast<int32_t>(total_secs % 86400);
    if (rem_secs < 0)
      rem_secs += 86400;

    const uint32_t hours = static_cast<uint32_t>(rem_secs / 3600);
    const uint32_t mins = static_cast<uint32_t>((rem_secs % 3600) / 60);
    const uint32_t secs = static_cast<uint32_t>(rem_secs % 60);

    if (!time_only) {
      const auto date = detail::civil_from_days(days);
      detail::format_unsigned(out, static_cast<uint32_t>(date.year), 10, false,
                              4);
      out.put('-');
      detail::format_unsigned(out, date.month, 10, false, 2);
      out.put('-');
      detail::format_unsigned(out, date.day, 10, false, 2);
    }

    if (!date_only && !time_only) {
      out.put('T');
    }

    if (!date_only) {
      detail::format_unsigned(out, hours, 10, false, 2);
      out.put(':');
      detail::format_unsigned(out, mins, 10, false, 2);
      out.put(':');
      detail::format_unsigned(out, secs, 10, false, 2);
      out.put('.');
      detail::format_unsigned(
          out, static_cast<uint32_t>(sub_ms < 0 ? -sub_ms : sub_ms), 10, false,
          3);
      out.put('Z');
    }
  }
};

// ============================================================================
// Formatter for std::chrono::time_point (Steady / Monotonic Clock Uptime)
// ============================================================================

template <typename Duration>
struct formatter<std::chrono::time_point<std::chrono::steady_clock, Duration>> {
  constexpr void parse(format_parse_context &) noexcept {}

  void
  format(const std::chrono::time_point<std::chrono::steady_clock, Duration> &tp,
         const sink &out) const noexcept {
    using namespace std::chrono;
    const auto dur = tp.time_since_epoch();
    const auto total_secs = duration_cast<seconds>(dur).count();
    const auto ms = duration_cast<milliseconds>(dur).count() % 1000;

    const uint32_t hours = static_cast<uint32_t>(total_secs / 3600);
    const uint32_t mins = static_cast<uint32_t>((total_secs % 3600) / 60);
    const uint32_t secs = static_cast<uint32_t>(total_secs % 60);

    // Format as [HH:MM:SS.mmm] uptime
    detail::format_unsigned(out, hours, 10, false, 2);
    out.put(':');
    detail::format_unsigned(out, mins, 10, false, 2);
    out.put(':');
    detail::format_unsigned(out, secs, 10, false, 2);
    out.put('.');
    detail::format_unsigned(out, static_cast<uint32_t>(ms), 10, false, 3);
  }
};

} // namespace microfmt