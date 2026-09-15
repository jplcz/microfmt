// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

/** @file boost_describe.hpp @brief Boost.Describe integration for reflected types. */

#include "../microfmt.hpp"
#include <boost/describe.hpp>
#include <type_traits>

namespace microfmt {

namespace detail {

// Check if T has Boost.Describe members (only takes T)
template <typename T>
using is_described_struct = boost::describe::has_describe_members<T>;

// Check if E is an enum registered with Boost.Describe
template <typename E>
struct is_described_enum
    : std::integral_constant<
          bool, std::is_enum_v<E> &&
                    boost::describe::has_describe_enumerators<E>::value> {};

} // namespace detail

// ============================================================================
// Formatter for Boost.Describe Reflected Enums
// ============================================================================
template <typename E>
struct formatter<E, std::enable_if_t<detail::is_described_enum<E>::value>> {
  constexpr void parse(format_parse_context &ctx) noexcept { (void)ctx; }

  void format(E val, const sink &out) const noexcept {
    using Enumerators = boost::describe::describe_enumerators<E>;
    bool found = false;

    boost::mp11::mp_for_each<Enumerators>([&](auto D) noexcept {
      if (!found && D.value == val) {
        out.write(D.name);
        found = true;
      }
    });

    if (!found) {
      // Fallback: format raw underlying integer if value is unmapped/invalid
      using Underlying = std::underlying_type_t<E>;
      format_to(out, MICROFMT_STRING("static_cast<{}>({})"),
                sizeof(Underlying) == 1   ? "uint8_t"
                : sizeof(Underlying) == 2 ? "uint16_t"
                                          : "uint32_t",
                static_cast<Underlying>(val));
    }
  }
};

// ============================================================================
// Formatter for Boost.Describe Reflected Structs & Classes
// ============================================================================
template <typename T>
struct formatter<T, std::enable_if_t<detail::is_described_struct<T>::value>> {
  constexpr void parse(format_parse_context &ctx) noexcept { (void)ctx; }

  void format(const T &val, const sink &out) const noexcept {
    using Members =
        boost::describe::describe_members<T, boost::describe::mod_public>;

    out.put('{');
    bool first = true;

    boost::mp11::mp_for_each<Members>([&](auto D) noexcept {
      if (!first) {
        out.write(", ");
      }
      first = false;

      // Output member name
      out.write(D.name);
      out.write(": ");

      // Format member value recursively via microfmt
      format_to(out, MICROFMT_STRING("{}"), val.*D.pointer);
    });

    out.put('}');
  }
};

} // namespace microfmt
