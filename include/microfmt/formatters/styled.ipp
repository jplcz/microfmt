// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file styled.ipp @brief Out-of-line bodies for formatter<styled_str_view>
 * (see styled.hpp). Included from styled.hpp itself, guarded on
 * MICROFMT_SHARED_PROVIDE_DEFINITIONS (see microfmt/detail/compat.hpp).
 * Never included directly. */

MICROFMT_API_CONSTEXPR void formatter<styled_str_view>::parse(format_parse_context &ctx) noexcept {
  auto spec = ctx.spec();
  if (spec.empty())
    return;

  size_t i = 0;
  char fill = ' ';

  // Check for fill character + alignment specifier (e.g., '*<20', '_^15')
  if (spec.size() >= 2 && (spec[1] == '<' || spec[1] == '>' || spec[1] == '^')) {
    fill = spec[0];
    if (spec[1] == '<')
      cfg.align = text_align::left;
    else if (spec[1] == '>')
      cfg.align = text_align::right;
    else if (spec[1] == '^')
      cfg.align = text_align::center;
    align_set = true;
    fill_set = true;
    i = 2;
  } else if (spec[0] == '<' || spec[0] == '>' || spec[0] == '^') {
    if (spec[0] == '<')
      cfg.align = text_align::left;
    else if (spec[0] == '>')
      cfg.align = text_align::right;
    else if (spec[0] == '^')
      cfg.align = text_align::center;
    align_set = true;
    fill_set = true;
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

MICROFMT_API void formatter<styled_str_view>::format(const styled_str_view &input, const sink &out) const noexcept {
  styled_str_view s = input;

  // Apply format specifier overrides if configured
  if (cfg.width > 0)
    s.width = cfg.width;
  if (fill_set)
    s.fill_char = cfg.fill_char;
  if (align_set)
    s.align = cfg.align;
  if (cfg.casing != text_case::none)
    s.casing = cfg.casing;
  if (cfg.quote != quote_style::none)
    s.quote = cfg.quote;
  if (cfg.max_len > 0) {
    s.max_len = cfg.max_len;
    s.ellipsis = cfg.ellipsis;
  }

  microfmt::string_view raw = s.text;
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

  const size_t total_pad = (s.width > visible_len) ? (s.width - visible_len) : 0;
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
