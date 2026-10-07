// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file reloco_debug.hpp
 * @brief `reloco::Debug<T>` specializations for common reloco types.
 *
 * Deliberately kept separate from `formatters/reloco.hpp`: that header
 * owns every `microfmt::formatter<T>` (`{}`/Display-style) specialization
 * for reloco types, and stays that way -- this header only ever adds
 * `reloco::Debug<T>` (`{:?}`/`as_debug()`-priority) specializations, never
 * a `formatter<T>`, so each customization point's specializations live in
 * their own respective place instead of being mixed together.
 *
 * Most reloco container/pointer types deliberately do **not** get a new
 * `Debug<T>` specialization here, because they do not need one: a type
 * with no `Debug<T>` of its own still gets fully correct debug-priority
 * recursion for free, through `formatters/reloco.hpp`'s existing
 * `formatter<T>` specializations, which are themselves built on
 * `microfmt::detail::element_formatter<T>` -- and `element_formatter`
 * already tries `formatter<T>`, then `Display<T>`, then `Debug<T>`, in
 * that order, for whatever element/pointee type it is given (see
 * `microfmt.hpp`). A `rc<T>`/`unique_ptr<T>`/`shared_ptr<T>`/`cow<T>`
 * wrapping a `T` that only has `Debug<T>` (e.g. a
 * `MICROFMT_REFLECT_FORMAT`-annotated or `tools/reflect_dump`-generated
 * struct) already renders that inner `Debug<T>` correctly today, with no
 * changes needed here.
 *
 * `reloco::any` is the one common reloco type that genuinely needs new
 * code, because it is the one case `formatters/reloco.hpp` structurally
 * cannot cover: it is type-erased, so no `formatter<reloco::any>` can
 * exist that knows how to format whatever value is actually held. All it
 * can report is *what type* is held -- and that must come from reloco's
 * own non-RTTI type identity (`reloco::type_id`, see `type_id.hpp`),
 * which is what `reloco::any::type_id()` already returns, **not** from
 * `typeid(T)`/`dynamic_cast`-style compiler RTTI: reloco does not require
 * RTTI to be enabled (`-fno-rtti`/`/GR-` remain fully supported), and its
 * `type_id_tag<T>::name()` only ever falls back to `typeid(T).name()` as
 * a deliberately opt-in, secondary source when the consumer has both
 * enabled RTTI *and* defined `RELOCO_IMPLICIT_TYPEID` (see
 * `type_id.hpp`) -- an explicitly `RELOCO_TYPE_ID_NAME`-registered name
 * always takes priority over that fallback either way. `Debug<any>`
 * below simply reuses `reloco::type_id`'s own existing priority instead
 * of re-implementing (or bypassing) it, by formatting through the
 * already-registered `formatter<reloco::type_id>` in
 * `formatters/reloco.hpp`.
 *
 * `Debug<reloco::error>` prints the enum member's own name (e.g.
 * `out_of_range`) by delegating to `formatter<reloco::error>` in
 * `formatters/reloco.hpp`, which owns the hand-written name table.
 *
 * `reloco::result<T>` (`= reloco::expected<T, reloco::error>`) also gets
 * a `Debug<T>`, formatting Rust's `Result` debug convention: `Ok(value)`
 * or `Err(error)`, recursing into the held value/error via
 * `microfmt::as_debug` (matching the "`Debug` always recurses via
 * `Debug`" rule `reflect_annotate.hpp`'s generated structs also follow).
 * The `expected<void, E>` partial specialization is covered separately,
 * since its `value()` returns `void` (nothing to recurse into on the
 * `Ok` side).
 *
 * `reloco::rc<T>`/`reloco::weak_rc<T>` get a `Debug<T>` too, even though
 * `formatters/reloco.hpp` already has a `formatter<T>` for both: unlike
 * every other smart-pointer wrapper covered by that fallback reasoning
 * above, `rc`/`weak_rc` expose a genuinely debug-only diagnostic --
 * `use_count()`, the live strong-reference count -- that their `{}`
 * Display output intentionally omits (it only prints the pointee's
 * value, matching every other smart pointer's Display convention). The
 * pointee itself is still rendered via `microfmt::as_debug`, so a pointee
 * with its own `Debug<T>` still recurses correctly.
 *
 * `reloco::flat_hash_set<T, ...>`/`reloco::flat_hash_map<Key, Mapped,
 * ...>` get a `Debug<T>` for the same reason: `size()`/`capacity()`/
 * `load_factor_permille()` (see `detail/flat_hash_base.hpp`) are
 * debug-only diagnostics about the table's internal state that its `{}`
 * Display output (just the element/entry list) intentionally omits.
 */

#include <microfmt/microfmt.hpp>
#include <reloco/any.hpp>
#include <reloco/error.hpp>
#include <reloco/flat_hash_map.hpp>
#include <reloco/flat_hash_set.hpp>
#include <reloco/rc.hpp>

#include "reloco.hpp" // formatter<reloco::type_id>, reused by Debug<any> below

namespace reloco {

/**
 * @brief `Debug<reloco::any>`: `any(<type name>)`, using reloco's own
 * `type_id`-based type identity -- never compiler RTTI -- to name the
 * held type. There is no way to format the held *value* generically (see
 * this file's header comment): `any` exposes no formatting vtable, only
 * a type identity, so this is necessarily limited to naming the type, not
 * printing its contents.
 *
 * `<type name>` is whatever `microfmt::formatter<reloco::type_id>`
 * already renders for `val.type_id()`: the name registered via
 * `RELOCO_TYPE_ID_NAME` for the held type, `<unnamed type>` if the held
 * type has no registered name, or `<no type>` if `val` is empty.
 */
template <> struct Debug<any> {
  static void format(const any &val, const sink &out) noexcept {
    out.write("any(");
    microfmt::format_to(out, "{}", val.type_id());
    out.put(')');
  }
};

/**
 * @brief `Debug<reloco::error>`: the enum member's own name (e.g.
 * `out_of_range`), via a hand-written name table -- kept in sync with
 * `error.hpp`'s member list by hand, since the enum has no
 * reflection/name-lookup helper of its own.
 */
template <> struct Debug<error> {
  static void format(const error &val, const sink &out) noexcept { microfmt::formatter<error>{}.format(val, out); }
};

/**
 * @brief `Debug<reloco::result<T>>` (`= Debug<reloco::expected<T,
 * reloco::error>>`): Rust's `Result` debug convention, `Ok(value)` or
 * `Err(error)`, recursing into the held value/error via
 * `microfmt::as_debug` so each side always prefers its own `Debug<T>`
 * over `formatter<T>`/`Display<T>` (matching the "`Debug` always
 * recurses via `Debug`" rule generated struct dumps also follow, see
 * `reflect_annotate.hpp`).
 */
template <typename T, typename E> struct Debug<expected<T, E>> {
  static void format(const expected<T, E> &val, const sink &out) noexcept {
    if (val.has_value()) {
      microfmt::format_to(out, "Ok({})", microfmt::as_debug(val.value()));
    } else {
      microfmt::format_to(out, "Err({})", microfmt::as_debug(val.error()));
    }
  }
};

/**
 * @brief `Debug<reloco::expected<void, E>>`: like the primary
 * `Debug<expected<T, E>>` template above, but the `Ok` case has no value
 * to recurse into (`expected<void, E>::value()` returns `void`), so it is
 * printed as a bare `Ok` -- matching Rust's `Result<(), E>` `Debug`
 * output, `Ok(())`... except reloco has no unit/`()` type to print, so
 * this omits the parentheses' contents entirely rather than inventing one.
 */
template <typename E> struct Debug<expected<void, E>> {
  static void format(const expected<void, E> &val, const sink &out) noexcept {
    if (val.has_value()) {
      out.write("Ok()");
    } else {
      microfmt::format_to(out, "Err({})", microfmt::as_debug(val.error()));
    }
  }
};

/**
 * @brief `Debug<reloco::rc<T>>`: `rc(use_count: N) { value }`, or `rc(use_count:
 * 0) { (null) }` for an empty pointer -- unlike the `{}` Display output
 * (which just prints the pointee's value, matching every other smart
 * pointer), this reports the live strong-reference count, a genuinely
 * debug-only diagnostic. The pointee is rendered via `microfmt::as_debug`,
 * so a pointee with its own `Debug<T>` (e.g. a reflect-dump-generated
 * struct) still recurses correctly.
 */
template <typename T> struct Debug<rc<T>> {
  static void format(const rc<T> &val, const sink &out) noexcept {
    microfmt::format_to(out, "rc(use_count: {}) {{ ", val.use_count());
    if (!val) {
      out.write("(null)");
    } else {
      microfmt::format_to(out, "{}", microfmt::as_debug(*val.get()));
    }
    out.write(" }");
  }
};

/**
 * @brief `Debug<reloco::weak_rc<T>>`: like `Debug<rc<T>>` above, but
 * attempts `lock()` to read the pointee (the same fallback
 * `formatter<weak_rc<T>>` already uses): `rc(use_count: N) { value }` if
 * still alive, or `rc(use_count: 0) { (expired) }` once the last owning
 * `rc` has released it.
 */
template <typename T> struct Debug<weak_rc<T>> {
  static void format(const weak_rc<T> &val, const sink &out) noexcept {
    microfmt::format_to(out, "rc(use_count: {}) {{ ", val.use_count());
    auto locked = val.lock();
    if (!locked.has_value()) {
      out.write("(expired)");
    } else {
      microfmt::format_to(out, "{}", microfmt::as_debug(*locked.value().get()));
    }
    out.write(" }");
  }
};

/**
 * @brief `Debug<reloco::flat_hash_set<T, ...>>`: `flat_hash_set(size: N,
 * capacity: N, load_factor_permille: N) [val1, val2, ...]` -- the same
 * element list `{}` Display output already shows, prefixed with the
 * table's internal-state diagnostics (see `detail/flat_hash_base.hpp`'s
 * `load_factor_permille()`, an integer parts-per-thousand value; reloco
 * never uses floating point for diagnostics like this).
 */
template <typename T, typename Hash, typename KeyEqual> struct Debug<flat_hash_set<T, Hash, KeyEqual>> {
  static void format(const flat_hash_set<T, Hash, KeyEqual> &val, const sink &out) noexcept {
    microfmt::format_to(out, "flat_hash_set(size: {}, capacity: {}, load_factor_permille: {}) [", val.size(),
                        val.capacity(), val.load_factor_permille());
    bool is_first = true;
    for (const auto &elem : val) {
      if (!is_first) {
        out.write(", ");
      }
      is_first = false;
      microfmt::format_to(out, "{}", microfmt::as_debug(elem));
    }
    out.put(']');
  }
};

/**
 * @brief `Debug<reloco::flat_hash_map<Key, Mapped, ...>>`: like
 * `Debug<flat_hash_set<T, ...>>` above, but for `{key: val, ...}` entries.
 */
template <typename Key, typename Mapped, typename Hash, typename KeyEqual>
struct Debug<flat_hash_map<Key, Mapped, Hash, KeyEqual>> {
  static void format(const flat_hash_map<Key, Mapped, Hash, KeyEqual> &val, const sink &out) noexcept {
    microfmt::format_to(out, "flat_hash_map(size: {}, capacity: {}, load_factor_permille: {}) {{", val.size(),
                        val.capacity(), val.load_factor_permille());
    bool is_first = true;
    for (const auto &entry : val) {
      if (!is_first) {
        out.write(", ");
      }
      is_first = false;
      microfmt::format_to(out, "{}: {}", microfmt::as_debug(entry.first), microfmt::as_debug(entry.second));
    }
    out.put('}');
  }
};

} // namespace reloco
