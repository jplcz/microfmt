// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file boost_system.hpp @brief Boost.System error formatters. */

#include "../microfmt.hpp"
#include <boost/system/error_code.hpp>

namespace microfmt {

template <> struct formatter<boost::system::error_code> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const boost::system::error_code &error,
              const sink &out) const noexcept {
    microfmt::format_to(out, "{}:{}", error.category().name(),
                        error.value());
  }
};

template <> struct formatter<boost::system::error_condition> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const boost::system::error_condition &condition,
              const sink &out) const noexcept {
    microfmt::format_to(out, "{}:{}",
                        condition.category().name(), condition.value());
  }
};

} // namespace microfmt
