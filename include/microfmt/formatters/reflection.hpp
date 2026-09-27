// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file reflection.hpp
 * @brief Native P2996 (`-freflection`) formatters for enums and structs.
 *
 * Experimental header providing the same "reflected enum" and "reflected
 * struct" formatting `boost_describe.hpp` offers, without depending on
 * Boost or requiring a per-field description macro:
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
 * This header's own content only exists on a P2996-capable compiler (GCC
 * 16+ trunk as of this writing) built with `-std=c++26 -freflection`:
 * under any other build, including it compiles to nothing at all, the
 * same way `reflect_annotate.hpp` behaves, so a project can include this
 * header unconditionally without its own `#if RELOCO_HAS_REFLECTION`
 * guard around the `#include` -- simply do not rely on the formatters it
 * would have defined when the feature isn't enabled.
 *
 * `MICROFMT_REFLECT_FORMAT` and `enable_reflect_format` are declared in
 * `reflect_annotate.hpp`, not here, and that header alone -- not this one
 * -- is what a type's own header should include: `reflect_annotate.hpp`
 * never requires `-freflection` either, so the same annotated type
 * definition compiles under every toolchain. Only an application that
 * wants the *live* formatting this header provides needs to include this
 * header itself (and needs `-freflection` for that formatting to exist).
 *
 * A build that cannot use `-freflection` at all is not left out: see
 * `tools/reflect_dump/` for a code generator that runs the same reflection
 * this header uses once, offline, on a `-freflection` toolchain, and
 * writes out an equivalent, plain-C++ `formatter<T>`/`formatter<E>`
 * specialization per type -- no reflection syntax at all -- for any
 * ordinary compiler to consume instead.
 *
 * See `docs/reflection.md` for the full writeup (shared with
 * `jplcz_reloco`) and `docs/formatters/ranges-and-structures.md` for
 * usage. */

#include "../microfmt.hpp"
#include "reflect_annotate.hpp"

#if RELOCO_HAS_REFLECTION

#include <meta>
#include <string_view>
#include <type_traits>

namespace microfmt::detail {

template <typename E> inline constexpr bool is_reflect_formattable_enum_v = std::is_enum_v<E>;

template <typename T>
inline constexpr bool is_reflect_formattable_struct_v = std::is_class_v<T> && enable_reflect_format<T>::value;

} // namespace microfmt::detail

namespace microfmt {

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

#endif // RELOCO_HAS_REFLECTION
