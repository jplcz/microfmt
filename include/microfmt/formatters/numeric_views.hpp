// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file numeric_views.hpp
 * @brief `thousands()` digit-grouping view for integers.
 *
 * See `docs/formatters/values-and-binary.md` for rendering rules and examples.
 */

#include "../microfmt.hpp"
#include <cstdint>
#include <type_traits>

namespace microfmt {

/**
 * @brief Integer rendered in decimal with a separator between digit groups.
 *
 * Holds the value by copy, so it is safe to return from functions.
 */
template <typename T, typename = std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>>
struct thousands_view {
  T value;
  char separator{','};
  uint8_t group_size{3}; // 0 disables grouping
};

/**
 * @brief Groups the decimal digits of @p value, e.g. `thousands(1234567)` -> `1,234,567`.
 * @param value Integer to render.
 * @param separator Character inserted between groups (use `'_'` for source-style literals).
 * @param group_size Digits per group; 0 disables grouping.
 */
template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
[[nodiscard]] constexpr auto thousands(T value, char separator = ',', uint8_t group_size = 3) noexcept {
  return thousands_view<T>{value, separator, group_size};
}

template <typename T> struct formatter<thousands_view<T>> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const thousands_view<T> &view, const sink &out) const noexcept {
    uint64_t magnitude = 0;
    if constexpr (std::is_signed_v<T>) {
      if (view.value < 0) {
        out.put('-');
        // Two's-complement negate in unsigned space so the minimum value does not overflow.
        magnitude = ~static_cast<uint64_t>(static_cast<int64_t>(view.value)) + 1ULL;
      } else {
        magnitude = static_cast<uint64_t>(view.value);
      }
    } else {
      magnitude = static_cast<uint64_t>(view.value);
    }

    size_t digits = 1;
    for (uint64_t rest = magnitude; rest >= 10; rest /= 10) {
      ++digits;
    }

    // Walk digits from the most significant using a decreasing power of ten.
    uint64_t divisor = 1;
    for (size_t i = 1; i < digits; ++i) {
      divisor *= 10;
    }
    for (size_t remaining = digits; remaining > 0; --remaining) {
      out.put(static_cast<char>('0' + (magnitude / divisor) % 10));
      divisor = divisor > 1 ? divisor / 10 : 1;
      if (view.group_size != 0 && remaining > 1 && ((remaining - 1) % view.group_size) == 0) {
        out.put(view.separator);
      }
    }
  }
};

} // namespace microfmt
