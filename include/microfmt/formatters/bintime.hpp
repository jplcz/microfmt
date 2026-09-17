// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file bintime.hpp @brief FreeBSD `bintime` and `sbintime_t` time formatting
 * views. */

#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace microfmt {

// ============================================================================
// FreeBSD Time Type Traits
// ============================================================================

namespace detail {

// Primary detection: checks for struct with .sec and .frac members
template <typename T, typename = void>
struct is_bintime_struct : std::false_type {};

template <typename T>
struct is_bintime_struct<T, std::void_t<decltype(std::declval<T>().sec),
                                        decltype(std::declval<T>().frac)>>
    : std::true_type {};

} // namespace detail

/**
 * @brief Customization point trait detecting FreeBSD-style binary time types.
 *
 * Defaults to structural detection of a `sec`/`frac` member pair, but may be
 * explicitly specialized for custom or wrapped types.
 *
 * @tparam T Candidate time type.
 */
template <typename T> struct is_bintime : detail::is_bintime_struct<T> {};

/**
 * @brief Convenience variable template for @ref is_bintime.
 * @tparam T Candidate time type.
 */
template <typename T>
inline constexpr bool is_bintime_v =
    is_bintime<std::remove_cv_t<std::remove_reference_t<T>>>::value;

/**
 * @brief Fractional-second precision selectors for time formatting.
 *
 * Each enumerator value encodes the corresponding number of fractional decimal
 * digits to emit.
 */
enum class time_precision : uint8_t {
  sec = 0,
  ms = 3,
  us = 6,
  ns = 9,
  ps = 12
};

// ============================================================================
// Zero-Float Scaling Helpers
// ============================================================================

namespace detail {

/**
 * @brief Converts a `bintime` fractional part into decimal-scaling digits.
 *
 * @param frac Fractional seconds expressed in 2^-64 units.
 * @param prec Target decimal precision.
 * @return Decimal-scaled fractional value, or `0` for `sec` precision.
 */
inline uint64_t bintime_frac_to_decimal(uint64_t frac,
                                        time_precision prec) noexcept {
  uint64_t multiplier = 1'000'000'000;
  switch (prec) {
  case time_precision::ms:
    multiplier = 1'000;
    break;
  case time_precision::us:
    multiplier = 1'000'000;
    break;
  case time_precision::ns:
    multiplier = 1'000'000'000;
    break;
  case time_precision::ps:
    multiplier = 1'000'000'000'000ULL;
    break;
  default:
    return 0;
  }

  constexpr uint64_t half_mask = 0xFFFFFFFFULL;
  const uint64_t frac_low = frac & half_mask;
  const uint64_t frac_high = frac >> 32;
  const uint64_t multiplier_low = multiplier & half_mask;
  const uint64_t multiplier_high = multiplier >> 32;

  const uint64_t low_product = frac_low * multiplier_low;
  const uint64_t middle =
      frac_high * multiplier_low + (low_product >> 32);
  const uint64_t middle_low = middle & half_mask;
  const uint64_t middle_high = middle >> 32;
  const uint64_t cross = middle_low + frac_low * multiplier_high;

  return frac_high * multiplier_high + middle_high + (cross >> 32);
}

/**
 * @brief Converts an `sbintime_t` fractional part into decimal-scaling digits.
 *
 * @param frac32 Fractional seconds expressed in 2^-32 units.
 * @param prec Target decimal precision.
 * @return Decimal-scaled fractional value.
 */
inline uint32_t sbintime_frac_to_decimal(uint32_t frac32,
                                         time_precision prec) noexcept {
  uint64_t multiplier = 1'000'000'000;
  switch (prec) {
  case time_precision::ms:
    multiplier = 1'000;
    break;
  case time_precision::us:
    multiplier = 1'000'000;
    break;
  case time_precision::ns:
    multiplier = 1'000'000'000;
    break;
  default:
    multiplier = 1'000'000'000;
    break;
  }
  return static_cast<uint32_t>((static_cast<uint64_t>(frac32) * multiplier) >>
                               32);
}

} // namespace detail

// ============================================================================
// Formatter for Any `is_bintime` Matching Type (e.g. native struct bintime)
// ============================================================================

/**
 * @brief Formatter for any type satisfying @ref is_bintime_v.
 *
 * Accepts the `m`/`3`, `u`/`6`, `n`/`9`, `p`/`1` precision suffixes and the
 * `r`/`R` flag to suppress the trailing `s` unit.
 *
 * @tparam T Binary time type with `sec`/`frac` members.
 */
template <typename T> struct formatter<T, std::enable_if_t<is_bintime_v<T>>> {
  /**
   * @brief Fractional-second precision used when rendering.
   */
  time_precision precision{time_precision::ns};
  /**
   * @brief Set to `false` (via `r`/`R`) to suppress the trailing `s` unit.
   */
  bool show_unit{true};

  /**
   * @brief Parses precision and unit flags from the format specifier.
   * @param ctx Format parse context exposing the specifier text.
   */
  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    for (char c : spec) {
      if (c == 'm' || c == '3')
        precision = time_precision::ms;
      else if (c == 'u' || c == '6')
        precision = time_precision::us;
      else if (c == 'n' || c == '9')
        precision = time_precision::ns;
      else if (c == 'p' || c == '1')
        precision = time_precision::ps;
      else if (c == 'r' || c == 'R')
        show_unit = false;
    }
  }

  /**
   * @brief Renders a binary time value as `sec.frac` with optional `s` unit.
   * @param bt The value to format (read-only via @p sec/@p frac members).
   * @param out Destination sink.
   */
  void format(const T &bt, const sink &out) const noexcept {
    int64_t sec = static_cast<int64_t>(bt.sec);
    uint64_t frac = static_cast<uint64_t>(bt.frac);

    if (sec < 0) {
      out.put('-');
      detail::format_unsigned(out, static_cast<uint64_t>(-sec), 10, false, 0);
    } else {
      detail::format_unsigned(out, static_cast<uint64_t>(sec), 10, false, 0);
    }

    if (precision != time_precision::sec) {
      out.put('.');
      uint64_t dec_frac = detail::bintime_frac_to_decimal(frac, precision);
      detail::format_unsigned(out, dec_frac, 10, false,
                              static_cast<int>(precision));
    }

    if (show_unit) {
      out.put('s');
    }
  }
};

// ============================================================================
// sbintime_t Wrapper View (disambiguates from standard int64_t)
// ============================================================================

/**
 * @brief Non-owning view disambiguating an `sbintime_t` value from a plain
 * `int64_t`.
 *
 * @tparam T Integral storage type holding the raw `sbintime_t` value.
 */
template <typename T> struct sbintime_view {
  /**
   * @brief Raw 64-bit `sbintime_t` value (signed fixed-point, 32.32).
   */
  int64_t sbt{0};
};

/**
 * @brief Wraps an integral `sbintime_t` value in an @ref sbintime_view.
 * @tparam T Integral type of @p val.
 * @param val Raw `sbintime_t` binary fractional-seconds value.
 * @return A view that can be formatted as `sec.frac`.
 */
template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
[[nodiscard]] constexpr auto as_sbintime(T val) noexcept {
  return sbintime_view<T>{static_cast<int64_t>(val)};
}

/**
 * @brief Formatter for @ref sbintime_view values.
 *
 * Accepts the `m`/`3`, `u`/`6`, `n`/`9` precision suffixes and the `r`/`R`
 * flag to suppress the trailing `s` unit.
 *
 * @tparam T Integral storage type of the wrapped `sbintime_t`.
 */
template <typename T> struct formatter<sbintime_view<T>> {
  /**
   * @brief Fractional-second precision used when rendering.
   */
  time_precision precision{time_precision::us};
  /**
   * @brief Set to `false` (via `r`/`R`) to suppress the trailing `s` unit.
   */
  bool show_unit{true};

  /**
   * @brief Parses precision and unit flags from the format specifier.
   * @param ctx Format parse context exposing the specifier text.
   */
  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    for (char c : spec) {
      if (c == 'm' || c == '3')
        precision = time_precision::ms;
      else if (c == 'u' || c == '6')
        precision = time_precision::us;
      else if (c == 'n' || c == '9')
        precision = time_precision::ns;
      else if (c == 'r' || c == 'R')
        show_unit = false;
    }
  }

  /**
   * @brief Renders an `sbintime_t` as `sec.frac` with optional `s` unit.
   * @param sv The view holding the raw 32.32 fixed-point value.
   * @param out Destination sink.
   */
  void format(const sbintime_view<T> &sv, const sink &out) const noexcept {
    const int64_t val = sv.sbt;
    uint64_t magnitude = 0;
    if (val < 0) {
      out.put('-');
      magnitude = static_cast<uint64_t>(-(val + 1)) + 1;
    } else {
      magnitude = static_cast<uint64_t>(val);
    }

    const uint64_t sec = magnitude >> 32;
    const uint32_t frac =
        static_cast<uint32_t>(magnitude & UINT64_C(0xFFFFFFFF));

    detail::format_unsigned(out, sec, 10, false, 0);

    if (precision != time_precision::sec) {
      out.put('.');
      uint32_t dec_frac = detail::sbintime_frac_to_decimal(frac, precision);
      detail::format_unsigned(out, dec_frac, 10, false,
                              static_cast<int>(precision));
    }

    if (show_unit) {
      out.put('s');
    }
  }
};

} // namespace microfmt