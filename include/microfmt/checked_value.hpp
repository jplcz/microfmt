// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file checked_value.hpp @brief Re-export of `reloco::checked_value`, a Rust-like move/consumption
 * safety wrapper (see `<reloco/checked_value.hpp>`).
 *
 * See [Lifetime safety](../../docs/lifetime-safety.md) for the underlying annotation conventions this
 * type builds on.
 */

#include <reloco/checked_value.hpp>

namespace microfmt {

using reloco::checked_value;

} // namespace microfmt
