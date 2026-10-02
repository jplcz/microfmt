// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

// std-interop-file: opt-in Debug<T> specializations for std::array/std::optional/std::variant/std::monostate.

/** @file std_debug.hpp
 * @brief `reloco::Debug<T>` specializations for heap-free standard library
 * types.
 *
 * Most builtins (`int`/`char`/`bool`/pointers/`std::nullptr_t`) and
 * heap-free standard containers/wrappers (`std::optional`, `std::variant`,
 * `std::monostate`, and every tuple-like type via `formatters/tuple.hpp`,
 * which covers both `std::pair` and `std::tuple`) already get fully correct
 * `{:?}`/`as_debug()` behavior for free, through their existing
 * `microfmt::formatter<T>` specialization: `as_debug()`'s priority chain
 * (`Debug<T>` -> `formatter<T>` -> `Display<T>`, see `microfmt.hpp`) only
 * ever reaches `formatter<T>` when there is no `Debug<T>`, and those
 * `formatter<T>` specializations already parse/honor the `?` debug flag
 * themselves (e.g. `microfmt::string_view`/`char`'s own quoting, see
 * `format_parse_context::consume_debug_flag`). Adding a `Debug<T>` for any
 * of those types would be pure duplication -- at best redundant, at worst a
 * behavioral regression (since `Debug<T>` takes priority and ignores
 * `spec`, it would silently stop honoring any format specifier the existing
 * `formatter<T>` supports).
 *
 * This header is only for standard types that have **no** `formatter<T>`
 * (nor `Display<T>`) at all today, so `as_debug()`/`{:?}` would otherwise
 * fail to compile for them:
 *
 * * `std::array<T, N>` -- deliberately excluded from
 *   `formatters/tuple.hpp`'s generic tuple-like formatter (it has `.data()`,
 *   reserved there for string/span-like types), and not covered by any
 *   range/container formatter either. Renders as `[val1, val2, ...]`,
 *   recursing into each element via `microfmt::as_debug` (matching the
 *   "`Debug` always recurses via `Debug`" rule generated struct dumps and
 *   `formatters/reloco_debug.hpp`'s specializations also follow).
 * * `std::bitset<N>` -- renders its bits directly, MSB-first, with no
 *   allocation (`std::bitset::to_string()` would need one): the same
 *   ordering `std::bitset`'s own `operator<<` uses.
 * * `std::reference_wrapper<T>` -- purely transparent: forwards to the
 *   referenced value's own `as_debug()` output with no wrapper decoration
 *   of its own, exactly like every other reference-like reloco type in
 *   `formatters/reloco.hpp`/`reloco_debug.hpp`.
 * * `std::byte` -- has no `operator<<`/formatter of its own in the standard
 *   library either; renders as a zero-padded lowercase hex byte (`0x2a`),
 *   the conventional raw-byte debug representation.
 *
 * Floating-point `Debug<T>` (`float`/`double`/`long double`) is
 * deliberately **not** included here: see `formatters/debug_float.hpp`, a
 * separate opt-in header (reloco/microfmt avoid floating point by default,
 * see `formatters/floating.hpp`'s own opt-in status).
 */

#include <array>
#include <bitset>
#include <cstddef>
#include <functional>
#include <microfmt/microfmt.hpp>

namespace reloco {

/**
 * @brief `Debug<std::array<T, N>>`: `[val1, val2, ...]`, recursing into each
 * element via `microfmt::as_debug`. `N == 0` renders as `[]`.
 */
template <typename T, std::size_t N> struct Debug<std::array<T, N>> {
  static void format(const std::array<T, N> &val, const sink &out) noexcept {
    out.put('[');
    bool is_first = true;
    for (const auto &elem : val) {
      if (!is_first) {
        out.write(", ");
      }
      is_first = false;
      microfmt::format_to(out, "{}", microfmt::as_debug(elem));
    }
    out.put(']');
  }
};

/**
 * @brief `Debug<std::bitset<N>>`: the bit string, MSB (index `N - 1`) first,
 * matching `std::bitset::operator<<`'s own bit order -- computed directly
 * from `test(i)`, with no `to_string()` allocation.
 */
template <std::size_t N> struct Debug<std::bitset<N>> {
  static void format(const std::bitset<N> &val, const sink &out) noexcept {
    for (std::size_t i = N; i > 0; --i) {
      out.put(val.test(i - 1) ? '1' : '0');
    }
  }
};

/**
 * @brief `Debug<std::reference_wrapper<T>>`: forwards directly to the
 * referenced value's own `as_debug()` output, with no wrapper decoration --
 * `reference_wrapper` is purely transparent.
 */
template <typename T> struct Debug<std::reference_wrapper<T>> {
  static void format(const std::reference_wrapper<T> &val, const sink &out) noexcept {
    microfmt::format_to(out, "{}", microfmt::as_debug(val.get()));
  }
};

/**
 * @brief `Debug<std::byte>`: a zero-padded lowercase hex byte, e.g. `0x2a`
 * -- `std::byte` has no `operator<<`/formatter of its own in the standard
 * library either, so this picks the conventional raw-byte debug
 * representation.
 */
template <> struct Debug<std::byte> {
  static void format(const std::byte &val, const sink &out) noexcept {
    out.write("0x");
    microfmt::detail::format_unsigned<microfmt::detail::radix::hex>(out, std::to_integer<unsigned>(val), false, 2);
  }
};

} // namespace reloco
