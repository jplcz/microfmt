// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file expected.hpp @brief Re-export of `reloco::expected`/`reloco::unexpected`, a `std::expected`
 * substitute for C++17 (see `<reloco/expected.hpp>`). */

#include <reloco/expected.hpp>

namespace microfmt {

using reloco::expected;
using reloco::expected_tag_t;
using reloco::unexpected;

} // namespace microfmt
