// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file string.hpp @brief `std::basic_string` and advanced string formatting
 * views (fill, align, width, precision, debug escaping). */

#include "../microfmt.hpp"
#include <cstddef>
#include <string>
#include <string_view>

namespace microfmt {

/**
 * @brief Formatter for `std::basic_string` / `std::basic_string_view` values.
 *
 * @tparam CharT Character type.
 * @tparam Traits Character trait type.
 * @tparam Alloc Allocator type.
 */
template <typename CharT, typename Traits, typename Alloc> struct formatter<std::basic_string<CharT, Traits, Alloc>> {
  /**
   * @brief No-op parse; plain strings accept no format specifier.
   */
  constexpr void parse(format_parse_context &) noexcept {}
  /**
   * @brief Writes the string contents verbatim.
   * @param val String to format.
   * @param out Destination sink.
   */
  void format(const std::basic_string<CharT, Traits, Alloc> &val, const sink &out) const noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

    out.write(std::basic_string_view<CharT, Traits>(val.data(), val.size()));

    RELOCO_END_UNSAFE_BUFFER_USAGE;
  }
};

namespace detail {

/**
 * @brief Horizontal alignment mode for padded string output.
 */
enum class string_align : uint8_t { none, left, right, center };

/**
 * @brief Parsed advanced string format specifier state.
 */
struct MICROFMT_API_CLASS parsed_string_spec {
  /**
   * @brief Fill character used for padding (default space).
   */
  char fill{' '};
  /**
   * @brief Requested horizontal alignment.
   */
  string_align align{string_align::none};
  /**
   * @brief Output field width.
   */
  size_t width{0};
  /**
   * @brief Maximum visible length, or `size_t(-1)` for none.
   */
  size_t precision{size_t(-1)};
  /**
   * @brief When `true`, render the string debug-escaped (`"..."`).
   */
  bool debug_escaped{false};
};

/**
 * @brief Parses an advanced string specifier:
 * `[[fill]align][width][.precision][?]`.
 * @param spec Raw specifier text.
 * @return Parsed @ref parsed_string_spec.
 */
inline constexpr parsed_string_spec parse_advanced_string_spec(microfmt::string_view spec) noexcept {
  parsed_string_spec res{};
  if (spec.empty()) {
    return res;
  }

  size_t pos = 0;
  const size_t len = spec.size();

  // Fill & Align: [[fill]align] (e.g. ">10", "*^10", "<5")
  if (pos + 1 < len && (spec[pos + 1] == '<' || spec[pos + 1] == '>' || spec[pos + 1] == '^')) {
    res.fill = spec[pos++];
    char a = spec[pos++];
    if (a == '<')
      res.align = string_align::left;
    else if (a == '>')
      res.align = string_align::right;
    else if (a == '^')
      res.align = string_align::center;
  } else if (pos < len && (spec[pos] == '<' || spec[pos] == '>' || spec[pos] == '^')) {
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
      res.precision = res.precision * 10 + static_cast<size_t>(spec[pos++] - '0');
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

/**
 * @brief Writes @p count copies of @p fill to the sink in bounded chunks.
 * @param out Destination sink.
 * @param fill Character to emit.
 * @param count Number of characters to emit.
 */
inline void write_fill_chars(const sink &out, char fill, size_t count) noexcept {
  char buf[32];
  std::fill_n(buf, std::min<size_t>(count, sizeof(buf)), fill);
  while (count > 0) {
    size_t chunk = std::min<size_t>(count, sizeof(buf));
    out.write(microfmt::string_view(buf, chunk));
    count -= chunk;
  }
}

/**
 * @brief Writes a single character with C-style debug escaping.
 * @param out Destination sink.
 * @param ch Character to escape and emit.
 */
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
    if (static_cast<unsigned char>(ch) < 0x20 || static_cast<unsigned char>(ch) == 0x7F) {
      static constexpr char hex_chars[] = "0123456789abcdef";
      char hex[4] = {'\\', 'x', hex_chars[(static_cast<unsigned char>(ch) >> 4) & 0x0F],
                     hex_chars[static_cast<unsigned char>(ch) & 0x0F]};
      out.write(microfmt::string_view(hex, 4));
    } else {
      out.write(microfmt::string_view(&ch, 1));
    }
    break;
  }
}

/**
 * @brief Computes the rendered character count of a debug-escaped string.
 * @param sv String to measure (including the surrounding quotes).
 * @return Rendered length in characters.
 */
inline size_t calculate_escaped_len(microfmt::string_view sv) noexcept {
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
      if (static_cast<unsigned char>(ch) < 0x20 || static_cast<unsigned char>(ch) == 0x7F) {
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

/**
 * @brief Non-owning string view supporting advanced format specifiers.
 */
class MICROFMT_API_CLASS as_string_view {
public:
  /**
   * @brief Constructs the view over a string_view.
   * @param str Text to format.
   */
  constexpr explicit as_string_view(microfmt::string_view str) noexcept : str_(str) {}

  /**
   * @brief Returns the underlying string view.
   * @return Referenced text.
   */
  [[nodiscard]] constexpr microfmt::string_view get() const noexcept { return str_; }

private:
  /// Referenced text.
  microfmt::string_view str_;
};

// ============================================================================
// Factory Functions: microfmt::as_string(...)
// ============================================================================

/**
 * @brief Wraps a string_view for advanced string formatting.
 * @param sv Text to format.
 * @return An @ref as_string_view.
 */
[[nodiscard]] constexpr as_string_view as_string(microfmt::string_view sv) noexcept { return as_string_view{sv}; }

/**
 * @brief Wraps a `std::basic_string` for advanced string formatting.
 * @tparam CharT Character type.
 * @tparam Traits Character trait type.
 * @tparam Alloc Allocator type.
 * @param str String to format.
 * @return An @ref as_string_view over the string's contents.
 */
template <typename CharT, typename Traits, typename Alloc>
[[nodiscard]] as_string_view as_string(const std::basic_string<CharT, Traits, Alloc> &str) noexcept {
  return as_string_view{microfmt::string_view(str.data(), str.size())};
}

template <typename CharT, typename Traits, typename Alloc>
[[nodiscard]] as_string_view as_string(std::basic_string<CharT, Traits, Alloc> &&) noexcept = delete;

/**
 * @brief Wraps a null-terminated C string for advanced string formatting.
 * @param str String to format, or `nullptr` (rendered as `(null)`).
 * @return An @ref as_string_view.
 */
[[nodiscard]] constexpr as_string_view as_string(const char *str) noexcept {
  return as_string_view{str ? microfmt::string_view(str) : microfmt::string_view("(null)")};
}

// ============================================================================
// Formatter for as_string_view
// ============================================================================

/**
 * @brief Formatter for @ref as_string_view supporting fill, alignment, width,
 * precision, and debug escaping.
 */
template <> struct formatter<as_string_view> {
  /**
   * @brief Parsed format state carried from @ref parse to @ref format.
   */
  detail::parsed_string_spec spec_{};

  /**
   * @brief Parses the advanced string specifier.
   * @param ctx Format parse context exposing the specifier text.
   */
  constexpr void parse(format_parse_context &ctx) noexcept { spec_ = detail::parse_advanced_string_spec(ctx.spec()); }

  /**
   * @brief Renders the string with the requested alignment/padding/escaping.
   * @param val The string view to format.
   * @param out Destination sink.
   */
  void format(const as_string_view &val, const sink &out) const noexcept {
    microfmt::string_view sv = val.get();

    // 1. Truncate to precision if requested
    if (spec_.precision != size_t(-1) && sv.size() > spec_.precision) {
      sv = sv.substr(0, spec_.precision);
    }

    // 2. Direct render path if debug escaping and padding are unused
    if (!spec_.debug_escaped && spec_.align == detail::string_align::none && spec_.width <= sv.size()) {
      out.write(sv);
      return;
    }

    const size_t visible_len = spec_.debug_escaped ? detail::calculate_escaped_len(sv) : sv.size();
    const size_t total_pad = (spec_.width > visible_len) ? (spec_.width - visible_len) : 0;

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
