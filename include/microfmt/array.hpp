// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file array.hpp @brief Re-export of `reloco::array`, a hardened, fixed-size `std::array`
 * alternative (see `<reloco/array.hpp>`). */

#include <reloco/array.hpp>

namespace microfmt {

using reloco::array;
using reloco::get;

} // namespace microfmt
