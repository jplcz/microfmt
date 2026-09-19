// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file reloco.hpp @brief Single re-export of every `reloco` type and annotation microfmt builds on.
 *
 * `reloco` (https://github.com/jplcz/reloco) owns lifetime-safety annotations and hardened,
 * checked container/wrapper types; microfmt injects them into the `microfmt` namespace so existing
 * `microfmt::X` call sites keep working unchanged. Macro annotations (e.g. `RELOCO_LIFETIMEBOUND`,
 * `RELOCO_BLOCK_RVALUE_ACCESS`) are used directly, with no `MICROFMT_*` alias, since macros have no
 * namespacing. See [Lifetime safety](../../docs/lifetime-safety.md) and
 * [Hardened containers](../../docs/hardened-containers.md) for the underlying conventions.
 */

#include <reloco/array.hpp>
#include <reloco/checked_value.hpp>
#include <reloco/expected.hpp>
#include <reloco/lifetime.hpp>
#include <reloco/rvalue_safety.hpp>
#include <reloco/span.hpp>
#include <reloco/string_view.hpp>
#include <reloco/value_ptr.hpp>
#include <reloco/value_ref.hpp>

// ============================================================================
// Unsafe Pointer Utilities & Unwrapping Boundaries
// ============================================================================

namespace microfmt::unsafe {

using reloco::unsafe::ptr_cast;
using reloco::unsafe::unchecked_address;

} // namespace microfmt::unsafe

namespace microfmt {

using reloco::array;
using reloco::checked_value;
using reloco::expected;
using reloco::expected_tag_t;
using reloco::get;
using reloco::unexpected;

using reloco::basic_string_view;
using reloco::string_view;
using reloco::string_view_error;
using reloco::wstring_view;

using reloco::span;
using reloco::span_error;

using reloco::value_ptr;
using reloco::value_ref;

} // namespace microfmt
