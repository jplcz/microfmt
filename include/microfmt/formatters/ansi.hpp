// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file ansi.hpp @brief ANSI terminal color and text-style formatting views. */

#include "../microfmt.hpp"
#include <cstdint>
#include <string_view>

namespace microfmt::ansi {

// ============================================================================
// ANSI Color and Attribute Bitflags
// ============================================================================

enum class color : uint8_t {
  none = 0,
  // Standard colors (30-37)
  black = 30,
  red = 31,
  green = 32,
  yellow = 33,
  blue = 34,
  magenta = 35,
  cyan = 36,
  white = 37,
  // Bright / High-Intensity colors (90-97)
  bright_black = 90, // Gray / Dark Gray
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

struct MICROFMT_API_CLASS style {
  color fg{color::none};
  color bg{color::none};
  attribute attr{attribute::none};

  static bool colors_enabled; // Runtime toggle for serial/non-TTY sinks
};

inline bool style::colors_enabled = true;

// Reset
inline constexpr style reset{};

// Standard Foreground Colors
inline constexpr style fg_black{color::black};
inline constexpr style fg_red{color::red};
inline constexpr style fg_green{color::green};
inline constexpr style fg_yellow{color::yellow};
inline constexpr style fg_blue{color::blue};
inline constexpr style fg_magenta{color::magenta};
inline constexpr style fg_cyan{color::cyan};
inline constexpr style fg_white{color::white};

// Bright Foreground Colors
inline constexpr style fg_gray{color::bright_black};
inline constexpr style fg_bright_black{color::bright_black};
inline constexpr style fg_bright_red{color::bright_red};
inline constexpr style fg_bright_green{color::bright_green};
inline constexpr style fg_bright_yellow{color::bright_yellow};
inline constexpr style fg_bright_blue{color::bright_blue};
inline constexpr style fg_bright_magenta{color::bright_magenta};
inline constexpr style fg_bright_cyan{color::bright_cyan};
inline constexpr style fg_bright_white{color::bright_white};

// Standard Background Colors
inline constexpr style bg_black{color::none, color::black};
inline constexpr style bg_red{color::none, color::red};
inline constexpr style bg_green{color::none, color::green};
inline constexpr style bg_yellow{color::none, color::yellow};
inline constexpr style bg_blue{color::none, color::blue};
inline constexpr style bg_magenta{color::none, color::magenta};
inline constexpr style bg_cyan{color::none, color::cyan};
inline constexpr style bg_white{color::none, color::white};

// Bright Background Colors
inline constexpr style bg_gray{color::none, color::bright_black};
inline constexpr style bg_bright_black{color::none, color::bright_black};
inline constexpr style bg_bright_red{color::none, color::bright_red};
inline constexpr style bg_bright_green{color::none, color::bright_green};
inline constexpr style bg_bright_yellow{color::none, color::bright_yellow};
inline constexpr style bg_bright_blue{color::none, color::bright_blue};
inline constexpr style bg_bright_magenta{color::none, color::bright_magenta};
inline constexpr style bg_bright_cyan{color::none, color::bright_cyan};
inline constexpr style bg_bright_white{color::none, color::bright_white};

// Text Attributes & Modifiers
inline constexpr style bold{color::none, color::none, attribute::bold};
inline constexpr style dim{color::none, color::none, attribute::dim};
inline constexpr style italic{color::none, color::none, attribute::italic};
inline constexpr style underline{color::none, color::none,
                                 attribute::underline};
inline constexpr style blink{color::none, color::none, attribute::blink};
inline constexpr style reverse{color::none, color::none, attribute::reverse};

// Semantic Status Styles
inline constexpr style error_style{color::bright_red, color::none,
                                   attribute::bold};
inline constexpr style warn_style{color::bright_yellow, color::none,
                                  attribute::bold};
inline constexpr style ok_style{color::bright_green, color::none,
                                attribute::bold};
inline constexpr style info_style{color::bright_cyan, color::none,
                                  attribute::none};

// ============================================================================
// Low-Level ANSI Code Emitter
// ============================================================================

MICROFMT_API void emit_style(const style &s, const sink &out) noexcept;
MICROFMT_API void emit_reset(const sink &out) noexcept;

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

template <typename T>
[[nodiscard]] constexpr styled_view<T> blue(const T &val) noexcept {
  return styled_view<T>{val, fg_blue};
}

template <typename T>
[[nodiscard]] constexpr styled_view<T> magenta(const T &val) noexcept {
  return styled_view<T>{val, fg_magenta};
}

template <typename T>
[[nodiscard]] constexpr styled_view<T> cyan(const T &val) noexcept {
  return styled_view<T>{val, fg_cyan};
}

template <typename T>
[[nodiscard]] constexpr styled_view<T> white(const T &val) noexcept {
  return styled_view<T>{val, fg_white};
}

template <typename T>
[[nodiscard]] constexpr styled_view<T> gray(const T &val) noexcept {
  return styled_view<T>{val, fg_gray};
}

} // namespace microfmt::ansi

#if MICROFMT_SHARED_PROVIDE_DEFINITIONS
#include "ansi.ipp"
#endif

// ============================================================================
// Formatter Specializations
// ============================================================================

namespace microfmt {

// Direct ANSI style control tokens: microfmt::format_to(out, "{}Text{}",
// ansi::fg_red, ansi::reset);
template <> struct formatter<ansi::style> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const ansi::style &st, const sink &out) const noexcept {
    ansi::emit_style(st, out);
  }
};

// Automatic scoped styling: microfmt::format_to(out, "Status: {}",
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
