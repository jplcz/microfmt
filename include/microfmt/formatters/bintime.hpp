// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

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

// Customization point: users can explicitly enable for custom/wrapped types
template <typename T> struct is_bintime : detail::is_bintime_struct<T> {};

template <typename T>
inline constexpr bool is_bintime_v = is_bintime<std::remove_cvref_t<T>>::value;

// Precision enum for formatting
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

#if defined(__SIZEOF_INT128__)
  unsigned __int128 prod = static_cast<unsigned __int128>(frac) * multiplier;
  return static_cast<uint64_t>(prod >> 64);
#else
  uint64_t fl = frac & 0xFFFFFFFFULL;
  uint64_t fh = frac >> 32;
  uint64_t pl = fl * multiplier;
  uint64_t ph = fh * multiplier + (pl >> 32);
  return ph >> 32;
#endif
}

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

template <typename T> struct formatter<T, std::enable_if_t<is_bintime_v<T>>> {
  time_precision precision{time_precision::ns};
  bool show_unit{true};

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
                              static_cast<size_t>(precision));
    }

    if (show_unit) {
      out.put('s');
    }
  }
};

// ============================================================================
// sbintime_t Wrapper View (disambiguates from standard int64_t)
// ============================================================================

template <typename T> struct sbintime_view {
  int64_t sbt{0};
};

template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
[[nodiscard]] constexpr auto as_sbintime(T val) noexcept {
  return sbintime_view<T>{static_cast<int64_t>(val)};
}

template <typename T> struct formatter<sbintime_view<T>> {
  time_precision precision{time_precision::us};
  bool show_unit{true};

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

  void format(const sbintime_view<T> &sv, const sink &out) const noexcept {
    int64_t val = sv.sbt;
    if (val < 0) {
      out.put('-');
      val = -val;
    }

    int64_t sec = val >> 32;
    uint32_t frac = static_cast<uint32_t>(val & 0xFFFFFFFFULL);

    detail::format_unsigned(out, static_cast<uint64_t>(sec), 10, false, 0);

    if (precision != time_precision::sec) {
      out.put('.');
      uint32_t dec_frac = detail::sbintime_frac_to_decimal(frac, precision);
      detail::format_unsigned(out, dec_frac, 10, false,
                              static_cast<size_t>(precision));
    }

    if (show_unit) {
      out.put('s');
    }
  }
};

} // namespace microfmt