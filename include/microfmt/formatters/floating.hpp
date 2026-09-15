// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

/** @file floating.hpp @brief Floating-point formatting backed by printf-style
 * specifiers. */

#include "../microfmt.hpp"
#include <cstddef>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace microfmt {

namespace detail {

/**
 * @brief Parsed floating-point format specifier state.
 */
struct parsed_float_spec {
  /**
   * @brief Optional sign character: `'+'`, `' '` or `'\0'` (none).
   */
  char sign{'\0'};      // '+', ' ', or '\0'
  /**
   * @brief Alternate-form flag (`#`), forcing a decimal point to be printed.
   */
  bool alt_form{false}; // '#' (force decimal point)
  /**
   * @brief Explicit precision, or `-1` when not specified.
   */
  int precision{-1};    // -1 if not specified
  /**
   * @brief Presentation type: `f`, `F`, `e`, `E`, `g`, `G`, `a` or `A`.
   */
  char type{'g'};       // 'f', 'F', 'e', 'E', 'g', 'G', 'a', 'A'
};

/**
 * @brief Parses a floating-point format specifier string.
 * @param spec Raw specifier text (e.g. `"+#.4e"`).
 * @return Parsed @ref parsed_float_spec describing the requested formatting.
 */
inline constexpr parsed_float_spec
parse_float_spec(std::string_view spec) noexcept {
  parsed_float_spec res{};
  if (spec.empty()) {
    return res;
  }

  size_t pos = 0;
  const size_t len = spec.size();

  // Sign option: ['+' | '-' | ' ']
  if (pos < len && (spec[pos] == '+' || spec[pos] == '-' || spec[pos] == ' ')) {
    if (spec[pos] != '-') {
      res.sign = spec[pos];
    }
    pos++;
  }

  // Alternate form: ['#']
  if (pos < len && spec[pos] == '#') {
    res.alt_form = true;
    pos++;
  }

  // Precision: ['.' precision]
  if (pos < len && spec[pos] == '.') {
    pos++;
    res.precision = 0;
    while (pos < len && spec[pos] >= '0' && spec[pos] <= '9') {
      res.precision = res.precision * 10 + (spec[pos++] - '0');
    }
  }

  // Type presentation: [f|F|e|E|g|G|a|A]
  if (pos < len) {
    char ch = spec[pos];
    if (ch == 'f' || ch == 'F' || ch == 'e' || ch == 'E' || ch == 'g' ||
        ch == 'G' || ch == 'a' || ch == 'A') {
      res.type = ch;
    }
  }

  return res;
}

// Build a standard printf format string (e.g., "%+#.4f" or "%Lg")
/**
 * @brief Builds a `printf`-style format string into a caller buffer.
 * @tparam T Floating type (adds the `L` prefix for `long double`).
 * @param dest Output buffer (must fit the produced format string).
 * @param spec Parsed format specifier to translate.
 * @return Number of bytes written to @p dest, excluding the null terminator.
 */
template <typename T>
inline size_t
build_printf_float_format(char *dest, const parsed_float_spec &spec) noexcept {
  size_t idx = 0;
  dest[idx++] = '%';

  if (spec.sign != '\0') {
    dest[idx++] = spec.sign;
  }
  if (spec.alt_form) {
    dest[idx++] = '#';
  }
  if (spec.precision >= 0) {
    dest[idx++] = '.';
    dest[idx++] = '*'; // dynamic precision passed via argument
  }

  if constexpr (std::is_same_v<T, long double>) {
    dest[idx++] = 'L';
  }

  dest[idx++] = spec.type;
  dest[idx] = '\0';
  return idx;
}

/**
 * @brief Formats a floating-point value via `snprintf` into the given sink.
 *
 * Falls back to a heap allocation only when the fixed stack buffer proves too
 * small for extreme precisions.
 *
 * @tparam T Floating type being formatted.
 * @param val The value to format.
 * @param spec Parsed format specifier.
 * @param out Destination sink.
 */
template <typename T>
inline void format_float_via_printf(T val, const parsed_float_spec &spec,
                                    const sink &out) noexcept {
  char fmt_buf[16];
  build_printf_float_format<T>(fmt_buf, spec);

  char buf[128];
  const auto print_float = [&](char *dest, size_t size) noexcept {
    if (spec.precision >= 0)
      return std::snprintf(dest, size, fmt_buf, spec.precision, val);
    return std::snprintf(dest, size, fmt_buf, val);
  };
  const int written = print_float(buf, sizeof(buf));

  if (written > 0) {
    if (static_cast<size_t>(written) < sizeof(buf)) {
      out.write(std::string_view(buf, static_cast<size_t>(written)));
    } else {
      // Stack buffer overflow fallback (rare for extreme precision)
      size_t heap_size = static_cast<size_t>(written) + 1;
      auto *heap_buf = new (std::nothrow) char[heap_size];
      if (heap_buf) {
        (void)print_float(heap_buf, heap_size);
        out.write(std::string_view(heap_buf, static_cast<size_t>(written)));
        delete[] heap_buf;
      }
    }
  }
}

} // namespace detail

// ============================================================================
// Formatter Specializations
// ============================================================================

/**
 * @brief Formatter for `float` values using printf-compatible presentation.
 */
template <> struct formatter<float> {
  /**
   * @brief Parsed state carried from @ref parse to @ref format.
   */
  detail::parsed_float_spec spec_{};

  /**
   * @brief Parses the floating-point format specifier.
   * @param ctx Format parse context exposing the specifier text.
   */
  constexpr void parse(format_parse_context &ctx) noexcept {
    spec_ = detail::parse_float_spec(ctx.spec());
  }

  /**
   * @brief Renders a `float` value.
   * @param val Value to format.
   * @param out Destination sink.
   */
  void format(float val, const sink &out) const noexcept {
    // Promoted to double for standard %f / %g printf conversions
    detail::format_float_via_printf<double>(static_cast<double>(val), spec_,
                                            out);
  }
};

/**
 * @brief Formatter for `double` values using printf-compatible presentation.
 */
template <> struct formatter<double> {
  /**
   * @brief Parsed state carried from @ref parse to @ref format.
   */
  detail::parsed_float_spec spec_{};

  /**
   * @brief Parses the floating-point format specifier.
   * @param ctx Format parse context exposing the specifier text.
   */
  constexpr void parse(format_parse_context &ctx) noexcept {
    spec_ = detail::parse_float_spec(ctx.spec());
  }

  /**
   * @brief Renders a `double` value.
   * @param val Value to format.
   * @param out Destination sink.
   */
  void format(double val, const sink &out) const noexcept {
    detail::format_float_via_printf<double>(val, spec_, out);
  }
};

/**
 * @brief Formatter for `long double` values using printf-compatible
 * presentation.
 */
template <> struct formatter<long double> {
  /**
   * @brief Parsed state carried from @ref parse to @ref format.
   */
  detail::parsed_float_spec spec_{};

  /**
   * @brief Parses the floating-point format specifier.
   * @param ctx Format parse context exposing the specifier text.
   */
  constexpr void parse(format_parse_context &ctx) noexcept {
    spec_ = detail::parse_float_spec(ctx.spec());
  }

  /**
   * @brief Renders a `long double` value.
   * @param val Value to format.
   * @param out Destination sink.
   */
  void format(long double val, const sink &out) const noexcept {
    detail::format_float_via_printf<long double>(val, spec_, out);
  }
};

} // namespace microfmt