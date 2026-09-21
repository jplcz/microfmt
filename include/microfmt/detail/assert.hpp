// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file assert.hpp @brief Re-export of reloco's checked-precondition facility.
 *
 * `RELOCO_ASSERT`/`RELOCO_DEBUG_ASSERT` (see `<reloco/detail/assert.hpp>`), and the
 * `RELOCO_KERNEL`/`RELOCO_KERNEL_PANIC`/`RELOCO_DISABLE_ASSERT*`/`RELOCO_DEBUG` customization
 * points that control them, are used directly rather than a `MICROFMT_*` alias. See
 * docs/porting.md for the full customization contract.
 */

#include "compat.hpp"

#include <reloco/detail/assert.hpp>

namespace microfmt {

} // namespace microfmt
