// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file boost_containers.hpp @brief Boost.Container sequence formatters. */

#include "../microfmt.hpp"
#include "ranges.hpp"
#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>

namespace microfmt {
namespace detail {

template <typename Container> struct boost_sequence_formatter {
  microfmt::string_view element_spec{};

  constexpr void parse(format_parse_context &ctx) noexcept {
    element_spec = ctx.spec();
  }

  void format(const Container &container, const sink &out) const noexcept {
    out.put('[');
    auto joined = microfmt::join(container);
    formatter<decltype(joined)> joined_formatter;
    format_parse_context element_context(element_spec);
    joined_formatter.parse(element_context);
    joined_formatter.format(joined, out);
    out.put(']');
  }
};

} // namespace detail

template <typename T, std::size_t Capacity, typename Options>
struct formatter<boost::container::static_vector<T, Capacity, Options>>
    : detail::boost_sequence_formatter<
          boost::container::static_vector<T, Capacity, Options>> {};

template <typename T, std::size_t N, typename Allocator, typename Options>
struct formatter<boost::container::small_vector<T, N, Allocator, Options>>
    : detail::boost_sequence_formatter<
          boost::container::small_vector<T, N, Allocator, Options>> {};

} // namespace microfmt
