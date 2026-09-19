// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "../microfmt.hpp"
#include <system_error>

namespace microfmt {

template <> struct formatter<std::error_category> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const std::error_category &category, const sink &out) const noexcept {
    const char *name = category.name();
    if (name) {
      out.write(name);
    } else {
      out.write("unknown_category");
    }
  }
};

template <> struct formatter<std::error_code> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const std::error_code &ec, const sink &out) const noexcept {
    // Print the descriptive message if available, otherwise fallback to category:value
    std::string msg = ec.message();
    if (!msg.empty()) {
      out.write(msg);
    }

    out.write(" [");

    // Format category name
    formatter<std::error_category> cat_fmt;
    cat_fmt.format(ec.category(), out);

    out.write(":");

    // Format error integer value
    formatter<int> int_fmt;
    int_fmt.format(ec.value(), out);

    out.write("]");
  }
};

} // namespace microfmt