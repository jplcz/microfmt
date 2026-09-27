// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file debug_float.hpp
 * @brief Opt-in `reloco::Debug<T>` specializations for `float`/`double`/
 * `long double`.
 *
 * Deliberately **not** pulled in by `formatters/std_debug.hpp` (or any
 * other header included by default): reloco/microfmt avoid floating point
 * everywhere by default -- `formatters/floating.hpp`'s `formatter<float>`/
 * `formatter<double>`/`formatter<long double>` are already an explicit,
 * separately-included opt-in for the same reason (kernel/bare-metal code
 * frequently cannot use the FPU at all without extra save/restore
 * ceremony). A consumer who has already opted into float support via
 * `floating.hpp` includes this header too, deliberately, to also get
 * `{:?}`/`as_debug()` support for those types.
 *
 * Uses `<cstdio>`'s `std::snprintf` directly (no `<iostream>`, matching
 * `floating.hpp`'s own approach) into a fixed stack buffer -- no heap
 * allocation. Rust's `f32`/`f64` `Debug` always shows a decimal point/
 * exponent even for whole-number values (`1.0`, not `1`); `%g` doesn't
 * guarantee that (e.g. `1` for `1.0f`), so a bare `.0` is appended
 * whenever the `%g` output has neither a `.` nor an `e`/`E` exponent and
 * isn't one of the `inf`/`nan` special values (which never take a
 * trailing `.0` either way).
 */

#include <microfmt/microfmt.hpp>
#include <cstddef>
#include <cstdio>
#include <type_traits>

namespace reloco {

namespace detail {

/**
 * @brief Shared `%g`/`%Lg`-based implementation behind every
 * `Debug<float/double/long double>::format` below.
 */
template <typename T> inline void format_debug_float(T val, const sink &out) noexcept {
  char buf[64];
  int written;
  if constexpr (std::is_same_v<T, long double>) {
    written = std::snprintf(buf, sizeof(buf), "%Lg", val);
  } else {
    written = std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(val));
  }
  if (written <= 0) {
    return;
  }

  const std::size_t len = static_cast<std::size_t>(written) < sizeof(buf) ? static_cast<std::size_t>(written)
                                                                           : sizeof(buf) - 1;
  const microfmt::string_view text(buf, len);
  out.write(text);

  const bool has_dot_or_exp =
      text.find('.') != microfmt::string_view::npos || text.find('e') != microfmt::string_view::npos ||
      text.find('E') != microfmt::string_view::npos;
  const bool is_special =
      text.find("inf") != microfmt::string_view::npos || text.find("nan") != microfmt::string_view::npos;
  if (!has_dot_or_exp && !is_special) {
    out.write(".0");
  }
}

} // namespace detail

/** @brief `Debug<float>`: see this file's header comment. */
template <> struct Debug<float> {
  static void format(float val, const sink &out) noexcept { detail::format_debug_float(val, out); }
};

/** @brief `Debug<double>`: see this file's header comment. */
template <> struct Debug<double> {
  static void format(double val, const sink &out) noexcept { detail::format_debug_float(val, out); }
};

/** @brief `Debug<long double>`: see this file's header comment. */
template <> struct Debug<long double> {
  static void format(long double val, const sink &out) noexcept { detail::format_debug_float(val, out); }
};

} // namespace reloco
