// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file repeated_view.hpp
 *  @brief Format a single value repeated a fixed number of times.
 *
 *  Use @ref microfmt::repeat to wrap any formattable value together with a
 *  repeat count and an optional separator. The view stores the value by
 *  decayed copy without allocation and formats it @c count times, writing the
 *  separator between consecutive repetitions. The complete replacement-field
 *  specifier is forwarded to the wrapped value's formatter on every
 *  repetition.
 */

#include "../microfmt.hpp"
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

namespace microfmt {

/** Non-owning repetition configuration wrapping a decayed value copy. */
template <typename T> struct repeated_view {
  T value;
  size_t count{0};
  microfmt::string_view separator{""};
};

/** Create a view that repeats @p value @p count times with no separator. */
template <typename T>
[[nodiscard]] constexpr auto repeat(T value, size_t count) noexcept {
  return repeated_view<std::decay_t<T>>{std::move(value), count, microfmt::string_view("")};
}

/** Create a view that repeats @p value @p count times, joined by @p separator. */
template <typename T>
[[nodiscard]] constexpr auto repeat(T value, size_t count, microfmt::string_view separator) noexcept {
  return repeated_view<std::decay_t<T>>{std::move(value), count, separator};
}

/** Formatter for @ref repeated_view.
 *
 *  The complete replacement-field specifier is forwarded to the wrapped
 *  value's formatter and re-applied for every repetition, for example
 *  `{:04X}`.
 */
template <typename T> struct formatter<repeated_view<T>> {
  microfmt::string_view forwarded_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept { forwarded_spec = ctx.spec(); }

  void format(const repeated_view<T> &rv, const sink &out) const noexcept {
    formatter<T> elem_fmt;
    format_parse_context elem_ctx(forwarded_spec);
    elem_fmt.parse(elem_ctx);

    for (size_t i = 0; i < rv.count; ++i) {
      if (i > 0) {
        out.write(rv.separator);
      }
      elem_fmt.format(rv.value, out);
    }
  }
};

} // namespace microfmt
