#pragma once

#include "microfmt.hpp"
#include <cstdint>
#include <string_view>

namespace microfmt::ansi {

// ============================================================================
// ANSI Color and Attribute Bitflags
// ============================================================================

enum class color : uint8_t {
  none = 0,
  black = 30,
  red = 31,
  green = 32,
  yellow = 33,
  blue = 34,
  magenta = 35,
  cyan = 36,
  white = 37,
  bright_black = 90,
  bright_red = 91,
  bright_green = 92,
  bright_yellow = 93,
  bright_blue = 94,
  bright_magenta = 95,
  bright_cyan = 96,
  bright_white = 97
};

enum class attribute : uint8_t {
  none = 0,
  bold = 1 << 0,
  dim = 1 << 1,
  italic = 1 << 2,
  underline = 1 << 3,
  blink = 1 << 4,
  reverse = 1 << 5
};

[[nodiscard]] constexpr attribute operator|(attribute a, attribute b) noexcept {
  return static_cast<attribute>(static_cast<uint8_t>(a) |
                                static_cast<uint8_t>(b));
}

[[nodiscard]] constexpr bool operator&(attribute a, attribute b) noexcept {
  return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

// ============================================================================
// Style Descriptor
// ============================================================================

struct style {
  color fg{color::none};
  color bg{color::none};
  attribute attr{attribute::none};

  static bool colors_enabled; // Runtime toggle for serial/non-TTY sinks
};

inline bool style::colors_enabled = true;

// Predefined style instances
inline constexpr style reset{};
inline constexpr style fg_red{color::red};
inline constexpr style fg_green{color::green};
inline constexpr style fg_yellow{color::yellow};
inline constexpr style fg_blue{color::blue};
inline constexpr style fg_magenta{color::magenta};
inline constexpr style fg_cyan{color::cyan};

inline constexpr style bold{color::none, color::none, attribute::bold};
inline constexpr style dim{color::none, color::none, attribute::dim};
inline constexpr style error_style{color::bright_red, color::none,
                                   attribute::bold};
inline constexpr style warn_style{color::bright_yellow, color::none,
                                  attribute::bold};
inline constexpr style ok_style{color::bright_green, color::none,
                                attribute::none};

// ============================================================================
// Low-Level ANSI Code Emitter
// ============================================================================

inline void emit_style(const style &s, const sink &out) noexcept {
  if (!style::colors_enabled) {
    return;
  }

  if (s.fg == color::none && s.bg == color::none && s.attr == attribute::none) {
    out.write("\033[0m");
    return;
  }

  out.write("\033[");
  bool first = true;

  auto emit_code = [&](uint8_t code) noexcept {
    if (!first)
      out.put(';');
    first = false;
    detail::format_unsigned(out, code, 10, false, 0);
  };

  // Attributes
  if (s.attr & attribute::bold)
    emit_code(1);
  if (s.attr & attribute::dim)
    emit_code(2);
  if (s.attr & attribute::italic)
    emit_code(3);
  if (s.attr & attribute::underline)
    emit_code(4);
  if (s.attr & attribute::blink)
    emit_code(5);
  if (s.attr & attribute::reverse)
    emit_code(7);

  // Foreground color
  if (s.fg != color::none) {
    emit_code(static_cast<uint8_t>(s.fg));
  }

  // Background color (fg code + 10)
  if (s.bg != color::none) {
    emit_code(static_cast<uint8_t>(s.bg) + 10);
  }

  out.put('m');
}

inline void emit_reset(const sink &out) noexcept {
  if (style::colors_enabled) {
    out.write("\033[0m");
  }
}

// ============================================================================
// Styled Value Wrapper
// ============================================================================

template <typename T> struct styled_view {
  const T &value;
  style st;
};

template <typename T>
[[nodiscard]] constexpr styled_view<T> styled(const T &val, style st) noexcept {
  return styled_view<T>{val, st};
}

template <typename T>
[[nodiscard]] constexpr styled_view<T> red(const T &val) noexcept {
  return styled_view<T>{val, fg_red};
}

template <typename T>
[[nodiscard]] constexpr styled_view<T> green(const T &val) noexcept {
  return styled_view<T>{val, fg_green};
}

template <typename T>
[[nodiscard]] constexpr styled_view<T> yellow(const T &val) noexcept {
  return styled_view<T>{val, fg_yellow};
}

} // namespace microfmt::ansi

// ============================================================================
// Formatter Specializations
// ============================================================================

namespace microfmt {

// 1. Direct ANSI style control tokens: microfmt::format_to(out, "{}Text{}",
// ansi::fg_red, ansi::reset);
template <> struct formatter<ansi::style> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const ansi::style &st, const sink &out) const noexcept {
    ansi::emit_style(st, out);
  }
};

// 2. Automatic scoped styling: microfmt::format_to(out, "Status: {}",
// ansi::red("FAILED"));
template <typename T> struct formatter<ansi::styled_view<T>> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const ansi::styled_view<T> &sv, const sink &out) const noexcept {
    ansi::emit_style(sv.st, out);

    // Format inner value
    formatter<std::remove_cv_t<std::remove_reference_t<T>>> inner_fmt;
    format_parse_context dummy_ctx("");
    inner_fmt.parse(dummy_ctx);
    inner_fmt.format(sv.value, out);

    ansi::emit_reset(out);
  }
};

} // namespace microfmt
