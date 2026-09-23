// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file binary.hpp @brief Binary integer formatting views and grouping options. */

#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace microfmt {

// ============================================================================
// Binary View Configuration
// ============================================================================

template <typename T,
          typename = std::enable_if_t<std::is_integral_v<T> &&
                                      !std::is_same_v<T, bool>>>
struct MICROFMT_API_CLASS binary_view {
  T value;
  uint8_t min_bits{sizeof(T) * 8}; // Default to full bit-width (8, 16, 32, 64)
  bool prefix{false};              // "0b" prefix
  bool group_nibbles{false};       // '0010_1010' separator
};

// ============================================================================
// Factory Helpers
// ============================================================================

// Fixed width matching type size (e.g., uint8_t -> 8 bits, uint16_t -> 16 bits)
template <typename T,
          std::enable_if_t<std::is_integral_v<T> &&
                               !std::is_same_v<T, bool>,
                           int> = 0>
[[nodiscard]] constexpr auto bin(T val) noexcept {
  return binary_view<T>{val, static_cast<uint8_t>(sizeof(T) * 8), false, false};
}

// Explicit bit width: microfmt::bin<8>(val)
template <uint8_t Bits, typename T,
          std::enable_if_t<std::is_integral_v<T> &&
                               !std::is_same_v<T, bool>,
                           int> = 0>
[[nodiscard]] constexpr auto bin(T val) noexcept {
  return binary_view<T>{val, Bits, false, false};
}

// With '0b' prefix and optional grouping
template <typename T,
          std::enable_if_t<std::is_integral_v<T> &&
                               !std::is_same_v<T, bool>,
                           int> = 0>
[[nodiscard]] constexpr auto bin_prefixed(T val,
                                          bool group_nibbles = false) noexcept {
  return binary_view<T>{val, static_cast<uint8_t>(sizeof(T) * 8), true,
                        group_nibbles};
}

// Grouped nibbles: 0010_1010
template <typename T,
          std::enable_if_t<std::is_integral_v<T> &&
                               !std::is_same_v<T, bool>,
                           int> = 0>
[[nodiscard]] constexpr auto bin_grouped(T val) noexcept {
  return binary_view<T>{val, static_cast<uint8_t>(sizeof(T) * 8), false, true};
}

// ============================================================================
// Formatter Specialization for binary_view
// ============================================================================

template <typename T> struct formatter<binary_view<T>> {
  bool parse_prefix{false};
  bool parse_grouped{false};
  uint8_t parse_width{0};

  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    for (char c : spec) {
      if (c == '#')
        parse_prefix = true;
      if (c == '_')
        parse_grouped = true;
    }
  }

  void format(const binary_view<T> &bv, const sink &out) const noexcept {
    const bool show_prefix = bv.prefix || parse_prefix;
    const bool group = bv.group_nibbles || parse_grouped;

    if (show_prefix) {
      out.write("0b");
    }

    using UnsignedT = std::make_unsigned_t<T>;
    const auto uval = static_cast<UnsignedT>(bv.value);

    const uint8_t bits =
        (bv.min_bits > 0) ? bv.min_bits : static_cast<uint8_t>(sizeof(T) * 8);

    for (int i = static_cast<int>(bits) - 1; i >= 0; --i) {
      const char bit_char = ((uval >> i) & 1U) ? '1' : '0';
      out.put(bit_char);

      // Add '_' separator between 4-bit nibbles
      if (group && (i % 4 == 0) && (i > 0)) {
        out.put('_');
      }
    }
  }
};

} // namespace microfmt