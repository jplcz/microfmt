// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file monad.hpp
 * @brief Optional and expected-like value formatting support. */

#include "../expected.hpp"
#include "../microfmt.hpp"
#include <optional>
#include <string_view>
#include <type_traits>

// std::expected is only available from C++23.
#if __cplusplus > 202002L
#if defined(__has_include) && __has_include(<expected>)
#include <expected>
#endif
#endif

#if __cplusplus > 202002L && defined(__cpp_lib_expected) &&                 \
    (__cpp_lib_expected >= 202202L)
#define MICROFMT_HAS_STD_EXPECTED 1
#else
#define MICROFMT_HAS_STD_EXPECTED 0
#endif

namespace microfmt {

// ============================================================================
// std::optional Formatter (C++17 / C++20)
// ============================================================================

template <typename T> struct formatter<std::optional<T>> {
  microfmt::string_view forwarded_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept {
    forwarded_spec = ctx.spec();
  }

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
// microfmt::expected Formatter (C++17+)
// ============================================================================

template <typename T, typename E> struct formatter<expected<T, E>> {
  microfmt::string_view forwarded_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept {
    forwarded_spec = ctx.spec();
  }

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

  constexpr void parse(format_parse_context &ctx) noexcept {
    forwarded_spec = ctx.spec();
  }

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