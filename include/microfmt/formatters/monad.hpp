// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file monad.hpp
 * @brief Optional and expected-like value formatting support. */

#include "../microfmt.hpp"
#include "../reloco.hpp"
#include <optional>
#include <reloco/optional.hpp>
#include <string_view>
#include <type_traits>

namespace microfmt {

// ============================================================================
// std::optional Formatter (C++17 / C++20)
// ============================================================================

template <typename T> struct formatter<std::optional<T>> {
  microfmt::string_view forwarded_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept { forwarded_spec = ctx.spec(); }

  void format(const std::optional<T> &opt, const sink &out) const noexcept {
    if (opt.has_value()) {
      out.write("Some(");
      formatter<T> inner_fmt;
      format_parse_context inner_ctx(forwarded_spec);
      inner_fmt.parse(inner_ctx);
      inner_fmt.format(*opt, out);
      out.put(')');
    } else {
      out.write("None");
    }
  }
};

// ============================================================================
// reloco::optional Formatter (C++17+)
// ============================================================================

/**
 * @brief Formatter for `reloco::optional<T>`.
 *
 * `reloco::optional` is not aliased into the `microfmt` namespace (unlike
 * `microfmt::expected`/`microfmt::checked_value`), so it is named explicitly
 * here. Renders the same way as `std::optional<T>` above: `Some(...)` /
 * `None`.
 */
template <typename T> struct formatter<reloco::optional<T>> {
  microfmt::string_view forwarded_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept { forwarded_spec = ctx.spec(); }

  void format(const reloco::optional<T> &opt, const sink &out) const noexcept {
    if (opt.has_value()) {
      out.write("Some(");
      formatter<T> inner_fmt;
      format_parse_context inner_ctx(forwarded_spec);
      inner_fmt.parse(inner_ctx);
      inner_fmt.format(opt.value(), out);
      out.put(')');
    } else {
      out.write("None");
    }
  }
};

// ============================================================================
// microfmt::checked_value Formatter (C++17+)
// ============================================================================

/**
 * @brief Formatter for `reloco::checked_value<T>` (aliased as
 * `microfmt::checked_value<T>`).
 *
 * Renders the held value directly -- no `Some(...)`/`Ok(...)` wrapper, since
 * a `checked_value` always holds a `T` once constructed, unlike
 * `optional`/`expected` -- or the literal text `<moved-from>` once the value
 * has been moved out of.
 */
template <typename T> struct formatter<checked_value<T>> {
  microfmt::string_view forwarded_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept { forwarded_spec = ctx.spec(); }

  void format(const checked_value<T> &val, const sink &out) const noexcept {
    if (val.is_moved_from()) {
      out.write("<moved-from>");
      return;
    }
    formatter<T> inner_fmt;
    format_parse_context inner_ctx(forwarded_spec);
    inner_fmt.parse(inner_ctx);
    inner_fmt.format(val.as_known().get(), out);
  }
};

// ============================================================================
// microfmt::expected Formatter (C++17+)
// ============================================================================

template <typename T, typename E> struct formatter<expected<T, E>> {
  microfmt::string_view forwarded_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept { forwarded_spec = ctx.spec(); }

  void format(const expected<T, E> &exp, const sink &out) const noexcept {
    if (exp.has_value()) {
      out.write("Ok(");
      if constexpr (!std::is_void_v<T>) {
        formatter<T> val_fmt;
        format_parse_context val_ctx(forwarded_spec);
        val_fmt.parse(val_ctx);
        val_fmt.format(exp.value(), out);
      }
      out.put(')');
    } else {
      out.write("Err(");
      formatter<E> err_fmt;
      format_parse_context err_ctx(forwarded_spec);
      err_fmt.parse(err_ctx);
      err_fmt.format(exp.error(), out);
      out.put(')');
    }
  }
};

// ============================================================================
// std::expected Formatter (C++23 conditional)
// ============================================================================

#if MICROFMT_HAS_STD_EXPECTED

template <typename T, typename E> struct formatter<std::expected<T, E>> {
  microfmt::string_view forwarded_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept { forwarded_spec = ctx.spec(); }

  void format(const std::expected<T, E> &exp, const sink &out) const noexcept {
    if (exp.has_value()) {
      out.write("Ok(");
      if constexpr (!std::is_void_v<T>) {
        formatter<T> val_fmt;
        format_parse_context val_ctx(forwarded_spec);
        val_fmt.parse(val_ctx);
        val_fmt.format(exp.value(), out);
      }
      out.put(')');
    } else {
      out.write("Err(");
      formatter<E> err_fmt;
      format_parse_context err_ctx(forwarded_spec);
      err_fmt.parse(err_ctx);
      err_fmt.format(exp.error(), out);
      out.put(')');
    }
  }
};

#endif // MICROFMT_HAS_STD_EXPECTED

} // namespace microfmt