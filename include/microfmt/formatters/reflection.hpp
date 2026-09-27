// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file reflection.hpp
 * @brief Native P2996 (`-freflection`) formatters for enums and structs.
 *
 * Experimental, opt-in header requiring a P2996-capable compiler (GCC 16+
 * trunk as of this writing) built with `-std=c++26 -freflection`; including
 * it under any other build is a hard compile error. It provides the same
 * "reflected enum" and "reflected struct" formatting `boost_describe.hpp`
 * offers, without depending on Boost or requiring a per-field description
 * macro:
 *
 * - every enum type gets an automatic `formatter<E>` that renders the
 *   enumerator's declared name (falling back to the underlying integer
 *   value for an unmapped/invalid value), unconditionally -- no opt-in
 *   needed, since no other builtin formatter targets raw enum types;
 * - a struct/class type gets an automatic `formatter<T>` that renders
 *   `{name: value, ...}` for its public non-static data members, but only
 *   once explicitly opted in via `MICROFMT_REFLECT_FORMAT(Type)` (or a
 *   direct `enable_reflect_format<Type>` specialization) -- mirroring
 *   Rust's `#[derive(Debug)]`, which is likewise per-type opt-in rather
 *   than a blanket default, both to avoid silently formatting a type an
 *   author intends to hand-write a formatter for later, and to avoid an
 *   ambiguous partial-specialization error against any other enable_if-based
 *   `formatter<T>` (including `boost_describe.hpp`'s) that might also
 *   match the same `T`.
 *
 * See `docs/reflection.md` for the underlying reflection mechanics (shared
 * with `jplcz_reloco`) and `docs/formatters/ranges-and-structures.md` for
 * usage. */

#include "../microfmt.hpp"

#if !RELOCO_HAS_REFLECTION
#error "microfmt/formatters/reflection.hpp requires a P2996-capable compiler built with -std=c++26 -freflection (see docs/reflection.md); do not include this header otherwise."
#endif

#include <meta>
#include <string_view>
#include <type_traits>

namespace microfmt {

/** @brief Opt-in customization point for `formatter<T>` struct reflection.
 *
 * Specialize to `std::true_type` (or use the `MICROFMT_REFLECT_FORMAT`
 * convenience macro below) to enable automatic `{name: value, ...}`
 * formatting of `T`'s public non-static data members via reflection. Left
 * at the default `std::false_type` for every type unless explicitly
 * opted in. */
template <typename T> struct enable_reflect_format : std::false_type {};

namespace detail {

template <typename E> inline constexpr bool is_reflect_formattable_enum_v = std::is_enum_v<E>;

template <typename T>
inline constexpr bool is_reflect_formattable_struct_v = std::is_class_v<T> && enable_reflect_format<T>::value;

} // namespace detail

// ============================================================================
// Formatter for reflected enums (automatic, no opt-in required)
// ============================================================================
template <typename E> struct formatter<E, std::enable_if_t<detail::is_reflect_formattable_enum_v<E>>> {
  constexpr void parse(format_parse_context &ctx) noexcept { (void)ctx; }

  void format(E val, const sink &out) const noexcept {
    template for (constexpr auto e : define_static_array(std::meta::enumerators_of(^^E))) {
      constexpr auto enumerator_value = [:e:];
      if (val == enumerator_value) {
        constexpr std::string_view name = std::meta::identifier_of(e);
        out.write(microfmt::string_view(name.data(), name.size()));
        return;
      }
    }

    // Fallback: format raw underlying integer if value is unmapped/invalid.
    format_to(out, "{}", static_cast<std::underlying_type_t<E>>(val));
  }
};

// ============================================================================
// Formatter for reflected structs & classes (opt-in via enable_reflect_format)
// ============================================================================
template <typename T> struct formatter<T, std::enable_if_t<detail::is_reflect_formattable_struct_v<T>>> {
  constexpr void parse(format_parse_context &ctx) noexcept { (void)ctx; }

  void format(const T &val, const sink &out) const noexcept {
    out.put('{');
    bool first = true;

    template for (constexpr auto member : define_static_array(
                      std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current()))) {
      if (!first) {
        out.write(", ");
      }
      first = false;

      constexpr std::string_view name = std::meta::identifier_of(member);
      out.write(microfmt::string_view(name.data(), name.size()));
      out.write(": ");

      // Format member value recursively via microfmt.
      format_to(out, "{}", val.[:member:]);
    }

    out.put('}');
  }
};

} // namespace microfmt

/** @brief Opts `Type` in to automatic reflection-based struct formatting.
 *
 * Equivalent to, but shorter than, writing the
 * `microfmt::enable_reflect_format<Type>` specialization by hand:
 *
 * ```cpp
 * struct point { int x; int y; };
 * MICROFMT_REFLECT_FORMAT(point);
 *
 * microfmt::format_to(out, "{}", point{3, 4}); // {x: 3, y: 4}
 * ```
 *
 * Unlike `BOOST_DESCRIBE_STRUCT`, no field list is needed -- reflection
 * enumerates `Type`'s public non-static data members itself -- so this
 * macro only ever needs `Type` and never has to be kept in sync with the
 * type's actual members. */
#define MICROFMT_REFLECT_FORMAT(Type) \
  template <> struct microfmt::enable_reflect_format<Type> : std::true_type {}
