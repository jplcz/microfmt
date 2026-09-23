// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file styled.hpp
 *  @brief Zero-allocation text casing, quoting, truncation, and alignment view.
 *
 *  Format @ref microfmt::styled_str_view values directly, or construct them
 *  with @ref microfmt::pad, @ref microfmt::to_upper,
 *  @ref microfmt::truncate, and @ref microfmt::quoted. Format specifiers use
 *  an optional fill and alignment (`*<`, `>`, or `^`), a width, and optional
 *  `u`, `l`, `t`, `q`, `b`, or `.N` transformation flags.
 */

#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace microfmt {

/** Text-case transformation applied while the view is formatted. */
enum class text_case : uint8_t { none = 0, upper, lower, title };

/** Alignment applied after transformations, quoting, and truncation. */
enum class text_align : uint8_t { left = 0, center, right };

/** Delimiters placed around the formatted text. */
enum class quote_style : uint8_t {
  none = 0,
  double_quotes, // "text"
  single_quotes, // 'text'
  brackets,      // [text]
  parens,        // (text)
  angle_brackets // <text>
};

/** Lightweight configuration for a styled text value.
 *
 *  Set @ref max_len to zero to disable truncation. When truncation is enabled,
 *  @ref ellipsis appends three dots when @ref max_len is greater than three.
 */
struct MICROFMT_API_CLASS styled_str_view {
  microfmt::string_view text{};
  size_t width{0};
  char fill_char{' '};
  text_align align{text_align::left};
  text_case casing{text_case::none};
  quote_style quote{quote_style::none};
  size_t max_len{0}; // 0 = no truncation
  bool ellipsis{true};
};

/** Pad text to at least @p width characters using the selected alignment. */
[[nodiscard]] constexpr styled_str_view pad(microfmt::string_view s, size_t width,
                                            text_align a = text_align::left,
                                            char fill = ' ') noexcept {
  return {s, width, fill, a, text_case::none, quote_style::none, 0, false};
}

/** Center text in a field of at least @p width characters. */
[[nodiscard]] constexpr styled_str_view
pad_center(microfmt::string_view s, size_t width, char fill = ' ') noexcept {
  return pad(s, width, text_align::center, fill);
}

/** Right-align text in a field of at least @p width characters. */
[[nodiscard]] constexpr styled_str_view
pad_right(microfmt::string_view s, size_t width, char fill = ' ') noexcept {
  return pad(s, width, text_align::right, fill);
}

/** Convert ASCII letters in text to uppercase while formatting. */
[[nodiscard]] constexpr styled_str_view to_upper(microfmt::string_view s) noexcept {
  return {s, 0,    ' ', text_align::left, text_case::upper, quote_style::none,
          0, false};
}

/** Convert ASCII letters in text to lowercase while formatting. */
[[nodiscard]] constexpr styled_str_view to_lower(microfmt::string_view s) noexcept {
  return {s, 0,    ' ', text_align::left, text_case::lower, quote_style::none,
          0, false};
}

/** Limit text to @p max_chars, optionally replacing its final three characters
 *  with an ellipsis when it is truncated. */
[[nodiscard]] constexpr styled_str_view
truncate(microfmt::string_view s, size_t max_chars,
         bool use_ellipsis = true) noexcept {
  return {s,
          0,
          ' ',
          text_align::left,
          text_case::none,
          quote_style::none,
          max_chars,
          use_ellipsis};
}

/** Surround text with the selected pair of delimiters. */
[[nodiscard]] constexpr styled_str_view
quoted(microfmt::string_view s,
       quote_style q = quote_style::double_quotes) noexcept {
  return {s, 0, ' ', text_align::left, text_case::none, q, 0, false};
}

/** Formatter for @ref styled_str_view.
 *
 *  The format specifier accepts `[fill][align][width][flags]`, where
 *  `align` is `<`, `>`, or `^`; flags are `u` (uppercase), `l` (lowercase),
 *  `t` (title case), `q` (double quotes), `b` (square brackets), and `.N`
 *  (truncate to N characters with an ellipsis when possible). Specifier
 *  settings override the corresponding view settings.
 */
template <> struct formatter<styled_str_view> {
  styled_str_view cfg{};
  bool align_set{false};
  bool fill_set{false};

  MICROFMT_API_CONSTEXPR void parse(format_parse_context &ctx) noexcept;

  MICROFMT_API void format(const styled_str_view &input, const sink &out) const noexcept;
};

#if MICROFMT_SHARED_PROVIDE_DEFINITIONS
#include "styled.ipp"
#endif

} // namespace microfmt