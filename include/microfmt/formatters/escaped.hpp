// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file escaped.hpp @brief Escaped text and binary-buffer formatting views. */

#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace microfmt {

// ============================================================================
// Escaped View Configuration & Adapter
// ============================================================================

struct escaped_view {
  span<const char> data{};
  bool quote{true};         // Wrap string in double quotes "..."
  bool escape_quotes{true}; // Escape inner " as \"
};

// ============================================================================
// Factory Functions
// ============================================================================

// From microfmt::string_view
[[nodiscard]] constexpr escaped_view
escaped(microfmt::string_view sv, bool quote = true,
        bool escape_quotes = true) noexcept {
  return escaped_view{span<const char>(sv.data(), sv.size()), quote,
                      escape_quotes};
}

// From raw const char* buffer / length
[[nodiscard]] constexpr escaped_view
escaped(const char *str, size_t len, bool quote = true,
        bool escape_quotes = true) noexcept {
  return escaped_view{span<const char>(str, len), quote, escape_quotes};
}

// From span of raw bytes (const uint8_t)
[[nodiscard]] inline escaped_view escaped(span<const uint8_t> bytes,
                                          bool quote = true,
                                          bool escape_quotes = true) noexcept {
  return escaped_view{
      span<const char>(reinterpret_cast<const char *>(bytes.data()),
                       bytes.size()),
      quote, escape_quotes};
}

// From C-style string literals / char arrays
template <size_t N>
[[nodiscard]] constexpr escaped_view
escaped(const char (&arr)[N], bool quote = true,
        bool escape_quotes = true) noexcept {
  // If null-terminated string literal, exclude trailing \0 from the slice
  // length
  const size_t len = (N > 0 && arr[N - 1] == '\0') ? N - 1 : N;
  return escaped_view{span<const char>(arr, len), quote, escape_quotes};
}

// From C-style byte arrays (uint8_t[N])
template <size_t N>
[[nodiscard]] constexpr escaped_view
escaped(const uint8_t (&arr)[N], bool quote = true,
        bool escape_quotes = true) noexcept {
  return escaped_view{span<const char>(reinterpret_cast<const char *>(arr), N),
                      quote, escape_quotes};
}

// ============================================================================
// Formatter Specialization for escaped_view
// ============================================================================

template <> struct formatter<escaped_view> {
  constexpr void parse(format_parse_context &ctx) noexcept { (void)ctx; }

  MICROFMT_API void format(const escaped_view &ev, const sink &out) const noexcept;
};

#if MICROFMT_SHARED_PROVIDE_DEFINITIONS
#include "escaped.ipp"
#endif

} // namespace microfmt