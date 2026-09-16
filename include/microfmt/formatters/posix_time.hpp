// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file posix_time.hpp @brief POSIX `struct timespec` and `struct timeval`
 * formatting views. */

#include "../microfmt.hpp"
#include <cstdint>
#include <type_traits>

namespace microfmt {

// ============================================================================
// Structural Traits for POSIX Time Types
// ============================================================================

namespace detail {

/**
 * @brief SFINAE trait detecting `timespec`-like types with `tv_sec`/`tv_nsec`.
 * @tparam T Candidate type.
 */
template <typename T, typename = void>
struct is_timespec_like : std::false_type {};

/**
 * @brief Specialization enabling @ref is_timespec_like for types exposing
 * `tv_sec` and `tv_nsec` members.
 * @tparam T Candidate type.
 */
template <typename T>
struct is_timespec_like<T, std::void_t<decltype(std::declval<T>().tv_sec),
                                       decltype(std::declval<T>().tv_nsec)>>
    : std::true_type {};

/**
 * @brief SFINAE trait detecting `timeval`-like types with `tv_sec`/`tv_usec`.
 * @tparam T Candidate type.
 */
template <typename T, typename = void>
struct is_timeval_like : std::false_type {};

/**
 * @brief Specialization enabling @ref is_timeval_like for types exposing
 * `tv_sec` and `tv_usec` members.
 * @tparam T Candidate type.
 */
template <typename T>
struct is_timeval_like<T, std::void_t<decltype(std::declval<T>().tv_sec),
                                      decltype(std::declval<T>().tv_usec)>>
    : std::true_type {};

} // namespace detail

/**
 * @brief Variable template for @ref detail::is_timespec_like.
 * @tparam T Candidate type.
 */
template <typename T>
inline constexpr bool is_timespec_v =
    detail::is_timespec_like<std::remove_cvref_t<T>>::value;

/**
 * @brief Variable template for @ref detail::is_timeval_like.
 * @tparam T Candidate type.
 */
template <typename T>
inline constexpr bool is_timeval_v =
    detail::is_timeval_like<std::remove_cvref_t<T>>::value;

// ============================================================================
// Formatter for struct timespec (.tv_sec, .tv_nsec)
// ============================================================================

/**
 * @brief Formatter for `struct timespec`-like types (`tv_sec`, `tv_nsec`).
 *
 * Accepts the `m`/`3`, `u`/`6`, `n`/`9` precision suffixes and the `r`/`R`
 * flag to suppress the trailing `s` unit.
 *
 * @tparam T Type satisfying @ref is_timespec_v.
 */
template <typename T> struct formatter<T, std::enable_if_t<is_timespec_v<T>>> {
  /**
   * @brief Fractional-digit precision (default 9 = nanoseconds).
   */
  uint8_t precision{9}; // default: 9 digits (nanoseconds)
  /**
   * @brief Set to `false` (via `r`/`R`) for raw seconds without `s` suffix.
   */
  bool show_unit{true}; // 'r' -> raw numbers without 's' suffix

  /**
   * @brief Parses precision and unit flags from the format specifier.
   * @param ctx Format parse context exposing the specifier text.
   */
  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    if (!spec.empty() && spec.front() == ':') {
      spec.remove_prefix(1);
    }
    for (char c : spec) {
      if (c == 'm' || c == '3')
        precision = 3;
      else if (c == 'u' || c == '6')
        precision = 6;
      else if (c == 'n' || c == '9')
        precision = 9;
      else if (c == 'r' || c == 'R')
        show_unit = false;
    }
  }

  /**
   * @brief Renders a `timespec` as `sec.frac` with optional `s` unit.
   * @param ts The value to format (read-only via @p tv_sec/@p tv_nsec).
   * @param out Destination sink.
   */
  void format(const T &ts, const sink &out) const noexcept {
    int64_t sec = static_cast<int64_t>(ts.tv_sec);
    uint32_t nsec = static_cast<uint32_t>(ts.tv_nsec);

    if (sec < 0) {
      out.put('-');
      detail::format_unsigned(out, static_cast<uint64_t>(-sec), 10, false, 0);
    } else {
      detail::format_unsigned(out, static_cast<uint64_t>(sec), 10, false, 0);
    }

    out.put('.');
    if (precision == 3) {
      detail::format_unsigned(out, nsec / 1'000'000, 10, false, 3);
    } else if (precision == 6) {
      detail::format_unsigned(out, nsec / 1'000, 10, false, 6);
    } else {
      detail::format_unsigned(out, nsec, 10, false, 9);
    }

    if (show_unit) {
      out.put('s');
    }
  }
};

// ============================================================================
// Formatter for struct timeval (.tv_sec, .tv_usec)
// ============================================================================

/**
 * @brief Formatter for `struct timeval`-like types (`tv_sec`, `tv_usec`).
 *
 * Accepts the `m`/`3`, `u`/`6` precision suffixes and the `r`/`R` flag to
 * suppress the trailing `s` unit.
 *
 * @tparam T Type satisfying @ref is_timeval_v.
 */
template <typename T> struct formatter<T, std::enable_if_t<is_timeval_v<T>>> {
  /**
   * @brief Fractional-digit precision (default 6 = microseconds).
   */
  uint8_t precision{6}; // default: 6 digits (microseconds)
  /**
   * @brief Set to `false` (via `r`/`R`) for raw seconds without `s` suffix.
   */
  bool show_unit{true};

  /**
   * @brief Parses precision and unit flags from the format specifier.
   * @param ctx Format parse context exposing the specifier text.
   */
  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    if (!spec.empty() && spec.front() == ':') {
      spec.remove_prefix(1);
    }
    for (char c : spec) {
      if (c == 'm' || c == '3')
        precision = 3;
      else if (c == 'u' || c == '6')
        precision = 6;
      else if (c == 'r' || c == 'R')
        show_unit = false;
    }
  }

  /**
   * @brief Renders a `timeval` as `sec.frac` with optional `s` unit.
   * @param tv The value to format (read-only via @p tv_sec/@p tv_usec).
   * @param out Destination sink.
   */
  void format(const T &tv, const sink &out) const noexcept {
    int64_t sec = static_cast<int64_t>(tv.tv_sec);
    uint32_t usec = static_cast<uint32_t>(tv.tv_usec);

    if (sec < 0) {
      out.put('-');
      detail::format_unsigned(out, static_cast<uint64_t>(-sec), 10, false, 0);
    } else {
      detail::format_unsigned(out, static_cast<uint64_t>(sec), 10, false, 0);
    }

    out.put('.');
    if (precision == 3) {
      detail::format_unsigned(out, usec / 1'000, 10, false, 3);
    } else {
      detail::format_unsigned(out, usec, 10, false, 6);
    }

    if (show_unit) {
      out.put('s');
    }
  }
};

} // namespace microfmt