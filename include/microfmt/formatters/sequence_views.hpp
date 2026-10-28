// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file sequence_views.hpp
 * @brief `enumerate()` and `zip()` views that complement `join()` from `ranges.hpp`.
 *
 * See `docs/formatters/ranges-and-structures.md` for rendering rules and examples.
 */

#include "../microfmt.hpp"
#include "ranges.hpp"
#include <iterator>
#include <type_traits>

namespace microfmt {

/**
 * @brief Non-owning view printing `index: value` for every element.
 *
 * Borrows the range; do not let it outlive the range (rvalue ranges are rejected).
 */
template <typename Range> struct RELOCO_POINTER enumerate_view {
  const Range *range;
  microfmt::string_view delimiter{", "};
};

/**
 * @brief Non-owning view printing `(a, b)` for each pair of elements; stops at the shorter range.
 */
template <typename R1, typename R2> struct RELOCO_POINTER zip_view {
  const R1 *first;
  const R2 *second;
  microfmt::string_view delimiter{", "};
};

template <typename Range, std::enable_if_t<detail::is_range<Range>::value, int> = 0>
[[nodiscard]] constexpr auto enumerate(const Range &range, microfmt::string_view delimiter = ", ") noexcept {
  return enumerate_view<Range>{&range, delimiter};
}

template <typename Range,
          std::enable_if_t<
              !std::is_lvalue_reference_v<Range> && detail::is_range<std::remove_reference_t<Range>>::value, int> = 0>
[[nodiscard]] constexpr auto enumerate(Range &&, microfmt::string_view = ", ") noexcept = delete;

template <typename R1, typename R2,
          std::enable_if_t<detail::is_range<R1>::value && detail::is_range<R2>::value, int> = 0>
[[nodiscard]] constexpr auto zip(const R1 &a, const R2 &b, microfmt::string_view delimiter = ", ") noexcept {
  return zip_view<R1, R2>{&a, &b, delimiter};
}

template <typename R1, typename R2,
          std::enable_if_t<(!std::is_lvalue_reference_v<R1> || !std::is_lvalue_reference_v<R2>) &&
                               detail::is_range<std::remove_reference_t<R1>>::value &&
                               detail::is_range<std::remove_reference_t<R2>>::value,
                           int> = 0>
[[nodiscard]] constexpr auto zip(R1 &&, R2 &&, microfmt::string_view = ", ") noexcept = delete;

namespace detail {
template <typename Range> using range_value_t = std::remove_cv_t<std::remove_reference_t<decltype(*std::begin(std::declval<const Range &>()))>>;
} // namespace detail

/** The element spec (`{:02x}`) is forwarded to every value. */
template <typename Range> struct formatter<enumerate_view<Range>> {
  microfmt::string_view elem_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept { elem_spec = ctx.spec(); }

  void format(const enumerate_view<Range> &view, const sink &out) const noexcept {
    detail::element_formatter<detail::range_value_t<Range>> element_fmt;
    format_parse_context elem_ctx(elem_spec);
    element_fmt.parse(elem_ctx);

    size_t index = 0;
    for (const auto &element : *view.range) {
      if (index != 0) {
        out.write(view.delimiter);
      }
      format_unsigned_index(out, index);
      out.write(": ");
      element_fmt.format(element, out);
      ++index;
    }
  }

private:
  static void format_unsigned_index(const sink &out, size_t index) noexcept {
    formatter<size_t> index_fmt;
    format_parse_context ctx(microfmt::string_view(""));
    index_fmt.parse(ctx);
    index_fmt.format(index, out);
  }
};

/** The element spec is forwarded to the values of both ranges. */
template <typename R1, typename R2> struct formatter<zip_view<R1, R2>> {
  microfmt::string_view elem_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept { elem_spec = ctx.spec(); }

  void format(const zip_view<R1, R2> &view, const sink &out) const noexcept {
    detail::element_formatter<detail::range_value_t<R1>> first_fmt;
    detail::element_formatter<detail::range_value_t<R2>> second_fmt;
    format_parse_context first_ctx(elem_spec);
    format_parse_context second_ctx(elem_spec);
    first_fmt.parse(first_ctx);
    second_fmt.parse(second_ctx);

    auto a = std::begin(*view.first);
    auto b = std::begin(*view.second);
    const auto a_end = std::end(*view.first);
    const auto b_end = std::end(*view.second);
    bool is_first = true;
    for (; a != a_end && b != b_end; ++a, ++b) {
      if (!is_first) {
        out.write(view.delimiter);
      }
      is_first = false;
      out.put('(');
      first_fmt.format(*a, out);
      out.write(", ");
      second_fmt.format(*b, out);
      out.put(')');
    }
  }
};

} // namespace microfmt
