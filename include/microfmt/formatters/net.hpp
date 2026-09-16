// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file net.hpp @brief Network address formatting views. */

#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace microfmt {

namespace detail {

template <typename T, typename = void> struct is_mac_container : std::false_type {};

template <typename T>
struct is_mac_container<
    T, std::void_t<decltype(std::declval<const T &>().data()), decltype(std::declval<const T &>().size())>>
    : std::integral_constant<bool, std::is_convertible_v<decltype(std::declval<const T &>().data()), const uint8_t *> &&
                                       std::is_convertible_v<decltype(std::declval<const T &>().size()), size_t>> {};

} // namespace detail

// ============================================================================
// MAC Address View Adapter
// ============================================================================

struct mac_view {
  span<const uint8_t> bytes{}; // 6 bytes (MAC-48/EUI-48) or 8 bytes (EUI-64)
  char separator{':'};
  bool uppercase{false};
};

// ============================================================================
// Factory Helpers
// ============================================================================

// From raw byte span / pointer + length
[[nodiscard]] constexpr mac_view mac(span<const uint8_t> bytes, char separator = ':', bool uppercase = false) noexcept {
  return mac_view{bytes, separator, uppercase};
}

// From 6-byte C-array
[[nodiscard]] constexpr mac_view mac(const uint8_t (&arr)[6], char separator = ':', bool uppercase = false) noexcept {
  return mac_view{span<const uint8_t>(arr, 6), separator, uppercase};
}

// From 8-byte EUI-64 C-array
[[nodiscard]] constexpr mac_view mac(const uint8_t (&arr)[8], char separator = ':', bool uppercase = false) noexcept {
  return mac_view{span<const uint8_t>(arr, 8), separator, uppercase};
}

// From std::array or container with .data() and .size()
template <typename ContainerT, std::enable_if_t<detail::is_mac_container<ContainerT>::value, int> = 0>
[[nodiscard]] constexpr mac_view mac(const ContainerT &c, char separator = ':', bool uppercase = false) noexcept {
  return mac_view{span<const uint8_t>(c.data(), c.size()), separator, uppercase};
}

template <typename ContainerT,
          std::enable_if_t<!std::is_lvalue_reference_v<ContainerT> &&
                               detail::is_mac_container<std::remove_reference_t<ContainerT>>::value,
                           int> = 0>
[[nodiscard]] constexpr mac_view mac(ContainerT &&, char = ':', bool = false) noexcept = delete;

// ============================================================================
// Formatter Specialization for mac_view
// ============================================================================

template <> struct formatter<mac_view> {
  char parse_separator{'\0'};
  bool parse_uppercase{false};

  constexpr void parse(format_parse_context &ctx) noexcept {
    for (char c : ctx.spec()) {
      if (c == 'X') {
        parse_uppercase = true;
      } else if (c == 'x') {
        parse_uppercase = false;
      } else if (c == '-' || c == ':' || c == '.' || c == '_') {
        parse_separator = c;
      }
    }
  }

  void format(const mac_view &mv, const sink &out) const noexcept {
    const size_t len = mv.bytes.size();
    if (len != 6 && len != 8) {
      out.write("00:00:00:00:00:00");
      return;
    }

    const char sep = (parse_separator != '\0') ? parse_separator : mv.separator;
    const bool is_upper = mv.uppercase || parse_uppercase;
    const auto &hex_digits = is_upper ? detail::hex_digits_upper : detail::hex_digits_lower;

    for (size_t i = 0; i < len; ++i) {
      const uint8_t b = mv.bytes[i];
      out.put(hex_digits[(b >> 4) & 0x0F]);
      out.put(hex_digits[b & 0x0F]);

      if (sep != '\0' && (i + 1) < len) {
        out.put(sep);
      }
    }
  }
};

} // namespace microfmt