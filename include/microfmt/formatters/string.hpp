// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "../microfmt.hpp"
#include <cstddef>
#include <string>
#include <string_view>

namespace microfmt {

template <typename CharT, typename Traits, typename Alloc>
struct formatter<std::basic_string<CharT, Traits, Alloc>> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const std::basic_string<CharT, Traits, Alloc> &val,
              const sink &out) const noexcept {
    out.write(std::basic_string_view<CharT, Traits>(val.data(), val.size()));
  }
};

namespace detail {

enum class string_align : uint8_t { none, left, right, center };

struct parsed_string_spec {
  char fill{' '};
  string_align align{string_align::none};
  size_t width{0};
  size_t precision{size_t(-1)};
  bool debug_escaped{false};
};

inline constexpr parsed_string_spec
parse_advanced_string_spec(std::string_view spec) noexcept {
  parsed_string_spec res{};
  if (spec.empty()) {
    return res;
  }

  size_t pos = 0;
  const size_t len = spec.size();

  // Fill & Align: [[fill]align] (e.g. ">10", "*^10", "<5")
  if (pos + 1 < len &&
      (spec[pos + 1] == '<' || spec[pos + 1] == '>' || spec[pos + 1] == '^')) {
    res.fill = spec[pos++];
    char a = spec[pos++];
    if (a == '<')
      res.align = string_align::left;
    else if (a == '>')
      res.align = string_align::right;
    else if (a == '^')
      res.align = string_align::center;
  } else if (pos < len &&
             (spec[pos] == '<' || spec[pos] == '>' || spec[pos] == '^')) {
    char a = spec[pos++];
    if (a == '<')
      res.align = string_align::left;
    else if (a == '>')
      res.align = string_align::right;
    else if (a == '^')
      res.align = string_align::center;
  }

  // Width: [width]
  while (pos < len && spec[pos] >= '0' && spec[pos] <= '9') {
    res.width = res.width * 10 + static_cast<size_t>(spec[pos++] - '0');
  }

  // Precision (truncation): [ .precision ]
  if (pos < len && spec[pos] == '.') {
    pos++;
    res.precision = 0;
    while (pos < len && spec[pos] >= '0' && spec[pos] <= '9') {
      res.precision =
          res.precision * 10 + static_cast<size_t>(spec[pos++] - '0');
    }
  }

  // Type / Mode: [s|?]
  while (pos < len) {
    if (spec[pos] == '?') {
      res.debug_escaped = true;
    }
    pos++;
  }

  // Default to left align if width is requested without explicit alignment
  if (res.width > 0 && res.align == string_align::none) {
    res.align = string_align::left;
  }

  return res;
}

inline void write_fill_chars(const sink &out, char fill,
                             size_t count) noexcept {
  char buf[32];
  std::fill_n(buf, std::min<size_t>(count, sizeof(buf)), fill);
  while (count > 0) {
    size_t chunk = std::min<size_t>(count, sizeof(buf));
    out.write(std::string_view(buf, chunk));
    count -= chunk;
  }
}

inline void write_escaped_character(const sink &out, char ch) noexcept {
  switch (ch) {
  case '\n':
    out.write("\\n");
    break;
  case '\r':
    out.write("\\r");
    break;
  case '\t':
    out.write("\\t");
    break;
  case '\\':
    out.write("\\\\");
    break;
  case '"':
    out.write("\\\"");
    break;
  case '\0':
    out.write("\\0");
    break;
  default:
    if (static_cast<unsigned char>(ch) < 0x20 ||
        static_cast<unsigned char>(ch) == 0x7F) {
      static constexpr char hex_chars[] = "0123456789abcdef";
      char hex[4] = {'\\', 'x',
                     hex_chars[(static_cast<unsigned char>(ch) >> 4) & 0x0F],
                     hex_chars[static_cast<unsigned char>(ch) & 0x0F]};
      out.write(std::string_view(hex, 4));
    } else {
      out.write(std::string_view(&ch, 1));
    }
    break;
  }
}

// Compute the rendered character count for escaped strings
inline size_t calculate_escaped_len(std::string_view sv) noexcept {
  size_t len = 2; // Surrounding quotes
  for (char ch : sv) {
    switch (ch) {
    case '\n':
    case '\r':
    case '\t':
    case '\\':
    case '"':
    case '\0':
      len += 2;
      break;
    default:
      if (static_cast<unsigned char>(ch) < 0x20 ||
          static_cast<unsigned char>(ch) == 0x7F) {
        len += 4; // \xNN
      } else {
        len += 1;
      }
      break;
    }
  }
  return len;
}

} // namespace detail

// ============================================================================
// as_string_view Wrapper Class
// ============================================================================

class as_string_view {
public:
  constexpr explicit as_string_view(std::string_view str) noexcept
      : str_(str) {}

  [[nodiscard]] constexpr std::string_view get() const noexcept { return str_; }

private:
  std::string_view str_;
};

// ============================================================================
// Factory Functions: microfmt::as_string(...)
// ============================================================================

[[nodiscard]] constexpr as_string_view as_string(std::string_view sv) noexcept {
  return as_string_view{sv};
}

template <typename CharT, typename Traits, typename Alloc>
[[nodiscard]] as_string_view
as_string(const std::basic_string<CharT, Traits, Alloc> &str) noexcept {
  return as_string_view{std::string_view(str.data(), str.size())};
}

[[nodiscard]] constexpr as_string_view as_string(const char *str) noexcept {
  return as_string_view{str ? std::string_view(str)
                            : std::string_view("(null)")};
}

// ============================================================================
// Formatter for as_string_view
// ============================================================================

template <> struct formatter<as_string_view> {
  detail::parsed_string_spec spec_{};

  constexpr void parse(format_parse_context &ctx) noexcept {
    spec_ = detail::parse_advanced_string_spec(ctx.spec());
  }

  void format(const as_string_view &val, const sink &out) const noexcept {
    std::string_view sv = val.get();

    // 1. Truncate to precision if requested
    if (spec_.precision != size_t(-1) && sv.size() > spec_.precision) {
      sv = sv.substr(0, spec_.precision);
    }

    // 2. Direct render path if debug escaping and padding are unused
    if (!spec_.debug_escaped && spec_.align == detail::string_align::none &&
        spec_.width <= sv.size()) {
      out.write(sv);
      return;
    }

    const size_t visible_len =
        spec_.debug_escaped ? detail::calculate_escaped_len(sv) : sv.size();
    const size_t total_pad =
        (spec_.width > visible_len) ? (spec_.width - visible_len) : 0;

    auto emit_body = [&]() {
      if (spec_.debug_escaped) {
        out.write("\"");
        for (char ch : sv) {
          detail::write_escaped_character(out, ch);
        }
        out.write("\"");
      } else {
        out.write(sv);
      }
    };

    if (total_pad == 0) {
      emit_body();
      return;
    }

    // 3. Padded Output
    switch (spec_.align) {
    case detail::string_align::left:
    case detail::string_align::none:
      emit_body();
      detail::write_fill_chars(out, spec_.fill, total_pad);
      break;

    case detail::string_align::right:
      detail::write_fill_chars(out, spec_.fill, total_pad);
      emit_body();
      break;

    case detail::string_align::center: {
      const size_t left_pad = total_pad / 2;
      const size_t right_pad = total_pad - left_pad;
      detail::write_fill_chars(out, spec_.fill, left_pad);
      emit_body();
      detail::write_fill_chars(out, spec_.fill, right_pad);
      break;
    }
    }
  }
};

} // namespace microfmt
