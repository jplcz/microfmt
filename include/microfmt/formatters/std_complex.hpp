// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

// std-interop-file: opt-in formatter for std::complex (floating-point parts).

/** @file std_complex.hpp
 * @brief Display formatter for `std::complex<T>`. Split from `std_types.hpp`
 * because it depends on the floating-point formatters.
 */

#include "../microfmt.hpp"
#include "floating.hpp"
#include <cmath>
#include <complex>
#include <type_traits>

namespace microfmt {

/**
 * @brief Formatter for `std::complex<T>` with floating-point `T`.
 *
 * Renders `(re+imi)` / `(re-imi)`. The spec is forwarded to both parts, so
 * `{:.2f}` gives `(1.50+2.25i)`.
 */
template <typename T> struct formatter<std::complex<T>, std::enable_if_t<std::is_floating_point_v<T>>> {
  microfmt::string_view forwarded_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept { forwarded_spec = ctx.spec(); }

  void format(const std::complex<T> &value, const sink &out) const noexcept {
    formatter<T> part;
    format_parse_context ctx(forwarded_spec);
    part.parse(ctx);

    out.put('(');
    part.format(value.real(), out);
    const T imag = value.imag();
    // signbit keeps "-0" and NaN-with-sign rendering consistent with the part formatter
    const bool negative = std::signbit(imag);
    out.put(negative ? '-' : '+');
    part.format(negative ? -imag : imag, out);
    out.write("i)");
  }
};

} // namespace microfmt
