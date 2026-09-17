// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file boost_monad.hpp @brief Boost optional, variant, and outcome formatters. */

#include "../microfmt.hpp"
#include <boost/optional.hpp>
#include <boost/outcome/result.hpp>
#include <boost/variant2/variant.hpp>
#include <type_traits>

namespace microfmt {

template <typename T> struct formatter<boost::optional<T>> {
  microfmt::string_view forwarded_spec{};

  constexpr void parse(format_parse_context &ctx) noexcept {
    forwarded_spec = ctx.spec();
  }

  void format(const boost::optional<T> &value, const sink &out) const noexcept {
    if (!value) {
      out.write("None");
      return;
    }

    out.write("Some(");
    formatter<T> inner;
    format_parse_context inner_context(forwarded_spec);
    inner.parse(inner_context);
    inner.format(*value, out);
    out.put(')');
  }
};

template <typename... Ts> struct formatter<boost::variant2::variant<Ts...>> {
  microfmt::string_view forwarded_spec{};
  bool show_index{false};

  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    if (!spec.empty() && (spec.front() == 'i' || spec.front() == '#')) {
      show_index = true;
      spec.remove_prefix(1);
    }
    forwarded_spec = spec;
  }

  void format(const boost::variant2::variant<Ts...> &value,
              const sink &out) const noexcept {
    if (show_index) {
      microfmt::format_to(out, MICROFMT_STRING("variant[{}]("), value.index());
    }

    boost::variant2::visit(
        [this, &out](const auto &active) noexcept {
          using active_type = std::decay_t<decltype(active)>;
          formatter<active_type> inner;
          format_parse_context inner_context(forwarded_spec);
          inner.parse(inner_context);
          inner.format(active, out);
        },
        value);

    if (show_index) {
      out.put(')');
    }
  }
};

template <typename R, typename S, typename NoValuePolicy>
struct formatter<boost::outcome_v2::basic_result<R, S, NoValuePolicy>> {
  microfmt::string_view forwarded_spec{};

  constexpr void parse(format_parse_context &ctx) noexcept {
    forwarded_spec = ctx.spec();
  }

  void
  format(const boost::outcome_v2::basic_result<R, S, NoValuePolicy> &result,
         const sink &out) const noexcept {
    if (result.has_value()) {
      out.write("Ok(");
      if constexpr (!std::is_void_v<R>) {
        formatter<R> inner;
        format_parse_context inner_context(forwarded_spec);
        inner.parse(inner_context);
        inner.format(result.assume_value(), out);
      }
      out.put(')');
      return;
    }

    if (result.has_error()) {
      out.write("Err(");
      formatter<S> inner;
      format_parse_context inner_context(forwarded_spec);
      inner.parse(inner_context);
      inner.format(result.assume_error(), out);
      out.put(')');
      return;
    }

    out.write("NoValue");
  }
};

} // namespace microfmt
