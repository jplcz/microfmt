// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file string_view.hpp @brief Re-export of `reloco::basic_string_view`, a hardened, checked
 * `std::basic_string_view` alternative (see `<reloco/string_view.hpp>`). */

#include <reloco/string_view.hpp>

namespace microfmt {

using reloco::basic_string_view;
using reloco::string_view;
using reloco::string_view_error;
using reloco::wstring_view;

} // namespace microfmt
