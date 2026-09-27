// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file reflect_annotate.hpp
 * @brief Always-safe reflection opt-in markers: `MICROFMT_REFLECT_FORMAT`,
 * `MICROFMT_REFLECT_DUMP_ENUM`, `MICROFMT_REFLECT_DUMP_STRUCT`.
 *
 * Unlike `reflection.hpp`, this header never requires `-freflection` and
 * never hard-errors without it. Include it directly next to a type's own
 * definition -- the same placement convention `RELOCO_TYPE_INSTANCE` uses
 * (see `jplcz_reloco`'s `docs/shared-library.md`) -- so the same,
 * unmodified header compiles under every toolchain a project supports:
 *
 * - under a plain (non-reflection) compiler, every macro here expands to
 *   nothing; the type is annotated with zero cost and zero risk;
 * - under a P2996-capable compiler (GCC 16+ trunk as of this writing)
 *   built with `-std=c++26 -freflection`, `MICROFMT_REFLECT_FORMAT`
 *   additionally opts the type in to `reflection.hpp`'s live formatter (see
 *   that header), and, only in a dedicated code-generator binary built
 *   with `-DMICROFMT_REFLECT_DUMP_MODE`, every macro here also registers
 *   the type so `microfmt::detail::render_reflect_dump()` can emit an
 *   equivalent, hand-written-looking `formatter<T>`/`formatter<E>`
 *   specialization as plain C++ text -- see `tools/reflect_dump/`.
 *
 * See `docs/reflection.md` for the full writeup, including exact build
 * commands for the generator and the generated header's own reflection
 * guard. */

#include "../detail/compat.hpp"

#include <type_traits>

namespace microfmt {

/** @brief Opt-in customization point for `formatter<T>` struct reflection.
 *
 * Specialize to `std::true_type` (or use the `MICROFMT_REFLECT_FORMAT`
 * macro below) to enable automatic `{name: value, ...}` formatting of
 * `T`'s public non-static data members via `reflection.hpp`'s live
 * formatter. Left at the default `std::false_type` for every type unless
 * explicitly opted in -- mirroring Rust's `#[derive(Debug)]`, which is
 * likewise per-type opt-in rather than a blanket default (see
 * `reflection.hpp` for why). This customization point exists, and is
 * declared here rather than in `reflection.hpp`, precisely so a type can
 * be opted in from a header that compiles under every toolchain. */
template <typename T> struct enable_reflect_format : std::false_type {};

} // namespace microfmt

#if RELOCO_HAS_REFLECTION

#include <meta>
#include <string>
#include <string_view>
#include <vector>

namespace microfmt::detail {

/** @brief One pending code-generator entry: `Type`'s name and its already
 * fully-rendered `formatter<Type>` specialization text. Populated only in
 * a `-DMICROFMT_REFLECT_DUMP_MODE` build (see `tools/reflect_dump/`). */
struct reflect_dump_entry {
  std::string type_name;
  std::string generated_code;
};

/** @brief Process-lifetime registry of every type/enum registered so far
 * via `MICROFMT_REFLECT_DUMP_ENUM`/`MICROFMT_REFLECT_DUMP_STRUCT`.
 * Populated by static initializers before `main()` runs, exactly like
 * GoogleTest's `TEST` registration. Only ever populated in a
 * `-DMICROFMT_REFLECT_DUMP_MODE` build. */
inline std::vector<reflect_dump_entry> &reflect_dump_registry() {
  static std::vector<reflect_dump_entry> registry;
  return registry;
}

/** @brief Renders a plain-text `formatter<E>` specialization for enum `E`
 * that switches on every declared enumerator by name, falling back to the
 * underlying integer for an unmapped value -- functionally identical to
 * `reflection.hpp`'s live formatter, but as ordinary source text with no
 * reflection syntax at all, safe for any legacy compiler to compile. */
template <typename E> std::string generate_reflect_enum_dump(std::string_view type_name) {
  std::string out;
  out += "template <> struct microfmt::formatter<";
  out += type_name;
  out += "> {\n  constexpr void parse(format_parse_context &ctx) noexcept { (void)ctx; }\n";
  out += "  void format(";
  out += type_name;
  out += " val, const sink &out) const noexcept {\n    switch (val) {\n";

  template for (constexpr auto e : define_static_array(std::meta::enumerators_of(^^E))) {
    constexpr std::string_view enumerator_name = std::meta::identifier_of(e);
    out += "    case ";
    out += type_name;
    out += "::";
    out += enumerator_name;
    out += ": out.write(\"";
    out += enumerator_name;
    out += "\"); return;\n";
  }

  out += "    default: break;\n    }\n    format_to(out, \"{}\", static_cast<std::underlying_type_t<";
  out += type_name;
  out += ">>(val));\n  }\n};\n";
  return out;
}

/** @brief Renders a plain-text `formatter<T>` specialization for struct/
 * class `T` that formats `{name: value, ...}` for every public non-static
 * data member by its literal name -- functionally identical to
 * `reflection.hpp`'s live formatter, but as ordinary source text with no
 * reflection syntax at all, safe for any legacy compiler to compile. */
template <typename T> std::string generate_reflect_struct_dump(std::string_view type_name) {
  std::string out;
  out += "template <> struct microfmt::formatter<";
  out += type_name;
  out += "> {\n  constexpr void parse(format_parse_context &ctx) noexcept { (void)ctx; }\n";
  out += "  void format(const ";
  out += type_name;
  out += " &val, const sink &out) const noexcept {\n    out.put('{');\n";

  bool first = true;
  template for (constexpr auto member : define_static_array(
                    std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current()))) {
    constexpr std::string_view name = std::meta::identifier_of(member);
    if (!first) {
      out += "    out.write(\", \");\n";
    }
    first = false;
    out += "    out.write(\"";
    out += name;
    out += ": \");\n    format_to(out, \"{}\", val.";
    out += name;
    out += ");\n";
  }

  out += "    out.put('}');\n  }\n};\n";
  return out;
}

template <typename E> bool register_reflect_dump_enum(std::string_view type_name) {
  reflect_dump_registry().push_back({std::string(type_name), generate_reflect_enum_dump<E>(type_name)});
  return true;
}

template <typename T> bool register_reflect_dump_struct(std::string_view type_name) {
  reflect_dump_registry().push_back({std::string(type_name), generate_reflect_struct_dump<T>(type_name)});
  return true;
}

/** @brief Renders the complete generated legacy-compiler header text from
 * every type registered so far. Called once by `tools/reflect_dump`'s
 * `main()`; every other consumer only ever reads the file it writes.
 *
 * The output is wrapped in `#if !RELOCO_HAS_REFLECTION` so that a build
 * that *does* have reflection available (e.g. a shared build
 * configuration that happens to include the generated header alongside
 * `reflection.hpp`) skips the frozen, potentially stale specializations
 * entirely and relies on the always-correct live formatter instead --
 * never both at once, which would otherwise be a duplicate-definition
 * error the moment the two disagreed about which types to cover. */
inline std::string render_reflect_dump() {
  std::string out = "// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>\n"
                     "//\n// SPDX-License-Identifier: BSD-2-Clause\n\n"
                     "// Auto-generated by tools/reflect_dump -- do not edit by hand.\n"
                     "// Regenerate with a -freflection build; see docs/reflection.md.\n\n"
                     "#pragma once\n\n"
                     "#include <microfmt/detail/compat.hpp>\n\n"
                     "#if !RELOCO_HAS_REFLECTION\n\n"
                     "#include <microfmt/microfmt.hpp>\n"
                     "#include <type_traits>\n\n";

  for (const auto &entry : reflect_dump_registry()) {
    out += entry.generated_code;
    out += "\n";
  }

  out += "#endif // !RELOCO_HAS_REFLECTION\n";
  return out;
}

} // namespace microfmt::detail

#if defined(MICROFMT_REFLECT_DUMP_MODE)

// Three levels of indirection are required, not two: `__COUNTER__` is only
// macro-expanded while being passed as a plain (non-`##`-adjacent)
// argument. `MICROFMT_REFLECT_DUMP_ENUM`/`_STRUCT` pass it to a pure
// forwarding macro first (`..._FWD`), which is what actually expands it
// before `..._IMPL` pastes the now-numeric token -- collapsing this to two
// levels silently pastes the literal text `__COUNTER__` instead.
#define MICROFMT_REFLECT_DUMP_ENUM_IMPL(Type, Counter)                                                               \
  static const bool microfmt_reflect_dump_enum_##Counter =                                                           \
      ::microfmt::detail::register_reflect_dump_enum<Type>(#Type)
#define MICROFMT_REFLECT_DUMP_ENUM_FWD(Type, Counter) MICROFMT_REFLECT_DUMP_ENUM_IMPL(Type, Counter)
#define MICROFMT_REFLECT_DUMP_ENUM(Type) MICROFMT_REFLECT_DUMP_ENUM_FWD(Type, __COUNTER__)

#define MICROFMT_REFLECT_DUMP_STRUCT_IMPL(Type, Counter)                                                             \
  static const bool microfmt_reflect_dump_struct_##Counter =                                                         \
      ::microfmt::detail::register_reflect_dump_struct<Type>(#Type)
#define MICROFMT_REFLECT_DUMP_STRUCT_FWD(Type, Counter) MICROFMT_REFLECT_DUMP_STRUCT_IMPL(Type, Counter)
#define MICROFMT_REFLECT_DUMP_STRUCT(Type) MICROFMT_REFLECT_DUMP_STRUCT_FWD(Type, __COUNTER__)

#else // !MICROFMT_REFLECT_DUMP_MODE

#define MICROFMT_REFLECT_DUMP_ENUM(Type)
#define MICROFMT_REFLECT_DUMP_STRUCT(Type)

#endif // MICROFMT_REFLECT_DUMP_MODE

/** @brief Opts `Type` in to reflection-based struct formatting.
 *
 * ```cpp
 * struct point { int x; int y; };
 * MICROFMT_REFLECT_FORMAT(point);
 *
 * microfmt::format_to(out, "{}", point{3, 4}); // {x: 3, y: 4}
 * ```
 *
 * Under a plain build (`reflection.hpp` not included, or included but not
 * `-freflection`), this only sets `enable_reflect_format<Type>` -- inert
 * on its own. Under a build that also includes `reflection.hpp`, it makes
 * `Type` render via that header's live formatter. Under
 * `tools/reflect_dump`'s dedicated generator binary
 * (`-DMICROFMT_REFLECT_DUMP_MODE`), it additionally registers `Type` for
 * `microfmt::detail::render_reflect_dump()`.
 *
 * Unlike `BOOST_DESCRIBE_STRUCT`, no field list is needed -- reflection
 * enumerates `Type`'s public non-static data members itself -- so this
 * macro only ever needs `Type` and never has to be kept in sync with the
 * type's actual members. */
#define MICROFMT_REFLECT_FORMAT(Type)                                                                                \
  template <> struct microfmt::enable_reflect_format<Type> : std::true_type {};                                      \
  MICROFMT_REFLECT_DUMP_STRUCT(Type)

#else // !RELOCO_HAS_REFLECTION

#define MICROFMT_REFLECT_FORMAT(Type)
#define MICROFMT_REFLECT_DUMP_ENUM(Type)
#define MICROFMT_REFLECT_DUMP_STRUCT(Type)

#endif // RELOCO_HAS_REFLECTION
