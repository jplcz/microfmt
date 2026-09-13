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
struct styled_str_view {
  std::string_view text{};
  size_t width{0};
  char fill_char{' '};
  text_align align{text_align::left};
  text_case casing{text_case::none};
  quote_style quote{quote_style::none};
  size_t max_len{0}; // 0 = no truncation
  bool ellipsis{true};
};

/** Pad text to at least @p width characters using the selected alignment. */
[[nodiscard]] constexpr styled_str_view pad(std::string_view s, size_t width,
                                            text_align a = text_align::left,
                                            char fill = ' ') noexcept {
  return {s, width, fill, a, text_case::none, quote_style::none, 0, false};
}

/** Center text in a field of at least @p width characters. */
[[nodiscard]] constexpr styled_str_view
pad_center(std::string_view s, size_t width, char fill = ' ') noexcept {
  return pad(s, width, text_align::center, fill);
}

/** Right-align text in a field of at least @p width characters. */
[[nodiscard]] constexpr styled_str_view
pad_right(std::string_view s, size_t width, char fill = ' ') noexcept {
  return pad(s, width, text_align::right, fill);
}

/** Convert ASCII letters in text to uppercase while formatting. */
[[nodiscard]] constexpr styled_str_view to_upper(std::string_view s) noexcept {
  return {s, 0,    ' ', text_align::left, text_case::upper, quote_style::none,
          0, false};
}

/** Convert ASCII letters in text to lowercase while formatting. */
[[nodiscard]] constexpr styled_str_view to_lower(std::string_view s) noexcept {
  return {s, 0,    ' ', text_align::left, text_case::lower, quote_style::none,
          0, false};
}

/** Limit text to @p max_chars, optionally replacing its final three characters
 *  with an ellipsis when it is truncated. */
[[nodiscard]] constexpr styled_str_view
truncate(std::string_view s, size_t max_chars,
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
quoted(std::string_view s,
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

  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    if (spec.empty())
      return;

    size_t i = 0;
    char fill = ' ';

    // Check for fill character + alignment specifier (e.g., '*<20', '_^15')
    if (spec.size() >= 2 &&
        (spec[1] == '<' || spec[1] == '>' || spec[1] == '^')) {
      fill = spec[0];
      if (spec[1] == '<')
        cfg.align = text_align::left;
      else if (spec[1] == '>')
        cfg.align = text_align::right;
      else if (spec[1] == '^')
        cfg.align = text_align::center;
      i = 2;
    } else if (spec[0] == '<' || spec[0] == '>' || spec[0] == '^') {
      if (spec[0] == '<')
        cfg.align = text_align::left;
      else if (spec[0] == '>')
        cfg.align = text_align::right;
      else if (spec[0] == '^')
        cfg.align = text_align::center;
      i = 1;
    }
    cfg.fill_char = fill;

    // Parse width
    size_t width = 0;
    while (i < spec.size() && spec[i] >= '0' && spec[i] <= '9') {
      width = width * 10 + static_cast<size_t>(spec[i] - '0');
      ++i;
    }
    if (width > 0)
      cfg.width = width;

    // Flags: casing ('u', 'l'), quotes ('q', 'b'), truncation ('.N')
    for (; i < spec.size(); ++i) {
      char c = spec[i];
      if (c == 'u' || c == 'U')
        cfg.casing = text_case::upper;
      else if (c == 'l' || c == 'L')
        cfg.casing = text_case::lower;
      else if (c == 't' || c == 'T')
        cfg.casing = text_case::title;
      else if (c == 'q')
        cfg.quote = quote_style::double_quotes;
      else if (c == 'b')
        cfg.quote = quote_style::brackets;
      else if (c == '.' && (i + 1) < spec.size()) {
        size_t max_len = 0;
        ++i;
        while (i < spec.size() && spec[i] >= '0' && spec[i] <= '9') {
          max_len = max_len * 10 + static_cast<size_t>(spec[i] - '0');
          ++i;
        }
        cfg.max_len = max_len;
        cfg.ellipsis = true;
        --i; // step back for outer loop
      }
    }
  }

  void format(const styled_str_view &input, const sink &out) const noexcept {
    styled_str_view s = input;

    // Apply format specifier overrides if configured
    if (cfg.width > 0)
      s.width = cfg.width;
    if (cfg.fill_char != ' ')
      s.fill_char = cfg.fill_char;
    if (cfg.align != text_align::left)
      s.align = cfg.align;
    if (cfg.casing != text_case::none)
      s.casing = cfg.casing;
    if (cfg.quote != quote_style::none)
      s.quote = cfg.quote;
    if (cfg.max_len > 0) {
      s.max_len = cfg.max_len;
      s.ellipsis = cfg.ellipsis;
    }

    std::string_view raw = s.text;
    bool truncated = false;

    if (s.max_len > 0 && raw.size() > s.max_len) {
      if (s.ellipsis && s.max_len > 3) {
        raw = raw.substr(0, s.max_len - 3);
        truncated = true;
      } else {
        raw = raw.substr(0, s.max_len);
      }
    }

    // Calculate visible length with quotes and ellipsis
    size_t visible_len = raw.size() + (truncated ? 3 : 0);
    if (s.quote != quote_style::none)
      visible_len += 2;

    const size_t total_pad =
        (s.width > visible_len) ? (s.width - visible_len) : 0;
    size_t pad_left = 0;
    size_t pad_right = 0;

    switch (s.align) {
    case text_align::right:
      pad_left = total_pad;
      break;
    case text_align::center:
      pad_left = total_pad / 2;
      pad_right = total_pad - pad_left;
      break;
    case text_align::left:
    default:
      pad_right = total_pad;
      break;
    }

    // Left padding
    for (size_t k = 0; k < pad_left; ++k)
      out.put(s.fill_char);

    // Opening Quote / Bracket
    switch (s.quote) {
    case quote_style::double_quotes:
      out.put('"');
      break;
    case quote_style::single_quotes:
      out.put('\'');
      break;
    case quote_style::brackets:
      out.put('[');
      break;
    case quote_style::parens:
      out.put('(');
      break;
    case quote_style::angle_brackets:
      out.put('<');
      break;
    case quote_style::none:
      break;
    }

    // Body with casing transformation
    for (size_t idx = 0; idx < raw.size(); ++idx) {
      char ch = raw[idx];
      switch (s.casing) {
      case text_case::upper:
        if (ch >= 'a' && ch <= 'z')
          ch = static_cast<char>(ch - ('a' - 'A'));
        break;
      case text_case::lower:
        if (ch >= 'A' && ch <= 'Z')
          ch = static_cast<char>(ch + ('a' - 'A'));
        break;
      case text_case::title:
        if (idx == 0 || raw[idx - 1] == ' ' || raw[idx - 1] == '_') {
          if (ch >= 'a' && ch <= 'z')
            ch = static_cast<char>(ch - ('a' - 'A'));
        } else {
          if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch + ('a' - 'A'));
        }
        break;
      case text_case::none:
        break;
      }
      out.put(ch);
    }

    if (truncated) {
      out.write("...");
    }

    // Closing Quote / Bracket
    switch (s.quote) {
    case quote_style::double_quotes:
      out.put('"');
      break;
    case quote_style::single_quotes:
      out.put('\'');
      break;
    case quote_style::brackets:
      out.put(']');
      break;
    case quote_style::parens:
      out.put(')');
      break;
    case quote_style::angle_brackets:
      out.put('>');
      break;
    case quote_style::none:
      break;
    }

    // Right padding
    for (size_t k = 0; k < pad_right; ++k)
      out.put(s.fill_char);
  }
};

} // namespace microfmt