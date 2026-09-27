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
 */

#include <microfmt/microfmt.hpp>
#include <reloco/any.hpp>

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

} // namespace reloco
