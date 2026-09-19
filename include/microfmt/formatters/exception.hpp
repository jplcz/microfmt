// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "../microfmt.hpp"
#include <exception>
#include <system_error>
#include <type_traits>

namespace microfmt {

template <typename T> struct formatter<T, std::enable_if_t<std::is_base_of_v<std::exception, T>>> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const T &value, const sink &out) const noexcept {
    const char *msg = value.what();
    if (msg) {
      out.write(msg);
    } else {
      out.write("<unknown exception>");
    }
  }
};

template <> struct formatter<std::system_error> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const std::system_error &value, const sink &out) const noexcept {
    const char *msg = value.what();
    if (msg) {
      out.write(msg);
    }

    auto ec = value.code();
    out.write(" [");
    out.write(ec.category().name());
    out.write(":");

    // Directly format the error value as an integer
    formatter<int> int_fmt;
    int_fmt.format(ec.value(), out);

    out.write("]");
  }
};

} // namespace microfmt