// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "../microfmt.hpp"
#include <system_error>

namespace microfmt {

template <> struct formatter<std::error_category> {
  constexpr void parse(format_parse_context &) noexcept {}

  MICROFMT_API void format(const std::error_category &category, const sink &out) const noexcept;
};

template <> struct formatter<std::error_code> {
  constexpr void parse(format_parse_context &) noexcept {}

  MICROFMT_API void format(const std::error_code &ec, const sink &out) const noexcept;
};

#if MICROFMT_SHARED_PROVIDE_DEFINITIONS
#include "error.ipp"
#endif

} // namespace microfmt