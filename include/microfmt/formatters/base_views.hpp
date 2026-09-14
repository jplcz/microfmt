// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

/** @file base_views.hpp @brief Base64 and configurable binary formatting views. */

#include "binary.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace microfmt {

// ============================================================================
// Base64 Stream View
// ============================================================================

struct base64_view {
  span<const uint8_t> data;
};

[[nodiscard]] constexpr base64_view base64(span<const uint8_t> bytes) noexcept {
  return base64_view{bytes};
}

template <size_t N>
[[nodiscard]] constexpr base64_view base64(const uint8_t (&arr)[N]) noexcept {
  return base64_view{span<const uint8_t>(arr, N)};
}

template <> struct formatter<base64_view> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const base64_view &bv, const sink &out) const noexcept {
    static constexpr char B64_TABLE[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    const size_t len = bv.data.size();
    size_t i = 0;

    while (i + 2 < len) {
      const uint32_t triple = (static_cast<uint32_t>(bv.data[i]) << 16) |
                              (static_cast<uint32_t>(bv.data[i + 1]) << 8) |
                              static_cast<uint32_t>(bv.data[i + 2]);
      out.put(B64_TABLE[(triple >> 18) & 0x3F]);
      out.put(B64_TABLE[(triple >> 12) & 0x3F]);
      out.put(B64_TABLE[(triple >> 6) & 0x3F]);
      out.put(B64_TABLE[triple & 0x3F]);
      i += 3;
    }

    if (i < len) {
      uint32_t triple = static_cast<uint32_t>(bv.data[i]) << 16;
      if (i + 1 < len) {
        triple |= static_cast<uint32_t>(bv.data[i + 1]) << 8;
        out.put(B64_TABLE[(triple >> 18) & 0x3F]);
        out.put(B64_TABLE[(triple >> 12) & 0x3F]);
        out.put(B64_TABLE[(triple >> 6) & 0x3F]);
        out.put('=');
      } else {
        out.put(B64_TABLE[(triple >> 18) & 0x3F]);
        out.put(B64_TABLE[(triple >> 12) & 0x3F]);
        out.put('=');
        out.put('=');
      }
    }
  }
};

// ============================================================================
// Grouped Binary / Radix View
// ============================================================================

template <typename T,
          typename = std::enable_if_t<std::is_integral_v<T> &&
                                      !std::is_same_v<T, bool>>>
struct bin_grouped_view {
  T value;
  uint8_t group_size{4};
  char separator{'_'};
};

template <typename T,
          std::enable_if_t<std::is_integral_v<T> &&
                               !std::is_same_v<T, bool>,
                           int> = 0>
[[nodiscard]] constexpr auto bin_grouped(T val, uint8_t group,
                                         char sep = '_') noexcept {
  return bin_grouped_view<T>{val, group, sep};
}

template <typename T> struct formatter<bin_grouped_view<T>> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const bin_grouped_view<T> &bg, const sink &out) const noexcept {
    using UnsignedT = std::make_unsigned_t<T>;
    constexpr size_t total_bits = sizeof(T) * 8;
    const auto raw = static_cast<UnsignedT>(bg.value);

    out.write("0b");
    for (size_t i = 0; i < total_bits; ++i) {
      const size_t bit_idx = total_bits - 1 - i;
      out.put((raw & (UnsignedT{1} << bit_idx)) ? '1' : '0');

      if (bg.group_size != 0 && (i + 1) < total_bits &&
          (bit_idx % bg.group_size == 0)) {
        out.put(bg.separator);
      }
    }
  }
};

} // namespace microfmt