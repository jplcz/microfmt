// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file span.hpp @brief Re-export of `reloco::span`, a hardened, checked `std::span` alternative
 * (see `<reloco/span.hpp>`). */

#include <reloco/span.hpp>

namespace microfmt {

using reloco::span;
using reloco::span_error;

} // namespace microfmt
