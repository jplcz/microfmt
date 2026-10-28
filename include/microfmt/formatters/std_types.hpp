// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

// std-interop-file: opt-in formatters for std::filesystem::path, std::bitset, std::byte and std::ratio.

/** @file std_types.hpp
 * @brief Display formatters for standard types that have no `operator<<`-free
 * equivalent in microfmt: `std::filesystem::path`, `std::bitset<N>`,
 * `std::byte` and `std::ratio<N, D>`.
 *
 * See `docs/formatters/values-and-binary.md` for rendering rules and examples.
 */

#include "../microfmt.hpp"
#include "string.hpp"
#include <bitset>
#include <cstddef>
#include <filesystem>
#include <ratio>
#include <string>
#include <type_traits>

namespace microfmt {

/**
 * @brief Formatter for `std::filesystem::path`.
 *
 * Writes the native path text unchanged. Accepts the same specifiers as
 * `std::string`. On platforms with a wide native encoding the path is first
 * converted with `path::string()` (which may allocate).
 */
template <> struct formatter<std::filesystem::path> {
  formatter<std::string> inner;

  constexpr void parse(format_parse_context &ctx) noexcept { inner.parse(ctx); }

  void format(const std::filesystem::path &p, const sink &out) const noexcept {
    // NOLINTNEXTLINE(reloco-noexcept): path::string() may allocate (std-interop contract)
    inner.format(p.string(), out);
  }
};

/**
 * @brief Formatter for `std::bitset<N>`.
 *
 * Renders bits MSB-first (index `N - 1` first), like `operator<<`. Never
 * allocates. `{}` prints plain `0`/`1` digits; `{:#}` prefixes `0b`.
 */
template <size_t N> struct formatter<std::bitset<N>> {
  bool prefix{false};

  constexpr void parse(format_parse_context &ctx) noexcept { prefix = ctx.spec() == microfmt::string_view("#"); }

  void format(const std::bitset<N> &bits, const sink &out) const noexcept {
    if (prefix) {
      out.write("0b");
    }
    for (size_t i = N; i > 0; --i) {
      out.put(bits.test(i - 1) ? '1' : '0');
    }
  }
};

/**
 * @brief Formatter for `std::byte`.
 *
 * Formats as the unsigned integer value, forwarding the spec to
 * `formatter<unsigned>` (so `{:02x}`, `{:08b}` etc. work).
 */
template <> struct formatter<std::byte> {
  formatter<unsigned> inner;

  constexpr void parse(format_parse_context &ctx) noexcept { inner.parse(ctx); }

  void format(std::byte value, const sink &out) const noexcept { inner.format(std::to_integer<unsigned>(value), out); }
};

/**
 * @brief Formatter for `std::ratio<N, D>` objects.
 *
 * Prints the reduced fraction as `num/den` (`1/1000` for `std::milli{}`); a
 * denominator of 1 prints just the numerator.
 */
template <intmax_t Num, intmax_t Den> struct formatter<std::ratio<Num, Den>> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(std::ratio<Num, Den>, const sink &out) const noexcept {
    using R = std::ratio<Num, Den>;
    detail::format_signed(out, R::num);
    if (R::den != 1) {
      out.put('/');
      detail::format_signed(out, R::den);
    }
  }
};

} // namespace microfmt
