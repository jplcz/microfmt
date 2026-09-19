// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file lifetime.hpp @brief Re-export of the reloco lifetime, access, nullability, and Clang
 * safe-buffers annotations.
 *
 * These annotations are defined by `reloco` as `RELOCO_*` macros (see `<reloco/lifetime.hpp>`); use
 * those macros directly rather than a `MICROFMT_*` alias. See
 * [Lifetime safety](../../docs/lifetime-safety.md) for the underlying annotation conventions.
 */

#include <reloco/lifetime.hpp>

// ============================================================================
// Unsafe Pointer Utilities & Unwrapping Boundaries
// ============================================================================

namespace microfmt::unsafe {

using reloco::unsafe::ptr_cast;
using reloco::unsafe::unchecked_address;

} // namespace microfmt::unsafe
