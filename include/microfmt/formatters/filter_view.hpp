// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file filter_view.hpp
 *  @brief Format only the elements of a range that satisfy a predicate.
 *
 *  Use @ref microfmt::filter with an iterable range or a raw pointer and
 *  count. The view stores the predicate without allocation and evaluates it
 *  as output is formatted. Use `b`, `c`, or `n` to select square brackets,
 *  braces, or no outer delimiters; the remaining specifier is forwarded to
 *  each matching element.
 */

#include "../microfmt.hpp"
#include <cstddef>
#include <iterator>
#include <string_view>
#include <type_traits>
#include <utility>

namespace microfmt {

/** Non-owning range plus predicate configuration for filtered formatting. */
template <typename Range, typename Predicate> struct filtered_range_view {
  const Range &range;
  Predicate predicate;
  char open_delim{'['};
  char close_delim{']'};
  microfmt::string_view separator{", "};
};

/** Non-owning pointer/count range plus predicate configuration. */
template <typename T, typename Predicate> struct filtered_pointer_range_view {
  const T *data{nullptr};
  size_t count{0};
  Predicate predicate;
  char open_delim{'['};
  char close_delim{']'};
  microfmt::string_view separator{", "};
};

/** Create a filtered view over @p rng using @p pred. */
template <typename Range, typename Predicate>
[[nodiscard]] constexpr auto filter(const Range &rng,
                                    Predicate &&pred) noexcept {
  return filtered_range_view<Range, std::decay_t<Predicate>>{
      rng, std::forward<Predicate>(pred)};
}

template <typename Range, typename Predicate,
          std::enable_if_t<!std::is_lvalue_reference_v<Range>, int> = 0>
[[nodiscard]] constexpr auto filter(Range &&,
                                    Predicate &&) noexcept = delete;

/** Create a filtered view over @p count elements beginning at @p ptr. */
template <typename T, typename Predicate>
[[nodiscard]] constexpr auto filter(const T *ptr, size_t count,
                                    Predicate &&pred) noexcept {
  return filtered_pointer_range_view<T, std::decay_t<Predicate>>{
      ptr, ptr == nullptr ? 0 : count, std::forward<Predicate>(pred)};
}

/** Formatter for @ref filtered_range_view.
 *
 *  `b` writes square brackets, `c` writes braces, and `n` omits outer
 *  delimiters. The remaining specifier is forwarded to every matching
 *  element, for example `{:c04X}`.
 */
template <typename Range, typename Predicate>
struct formatter<filtered_range_view<Range, Predicate>> {
  char open_c{'['};
  char close_c{']'};
  microfmt::string_view forwarded_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    if (spec.empty())
      return;

    size_t i = 0;
    if (spec[0] == 'b' || spec[0] == 'B') {
      open_c = '[';
      close_c = ']';
      i = 1;
    } else if (spec[0] == 'c' || spec[0] == 'C') {
      open_c = '{';
      close_c = '}';
      i = 1;
    } else if (spec[0] == 'n' || spec[0] == 'N') {
      open_c = '\0';
      close_c = '\0';
      i = 1;
    }
    forwarded_spec = spec.substr(i);
  }

  void format(const filtered_range_view<Range, Predicate> &fv,
              const sink &out) const noexcept {
    if (open_c != '\0') {
      out.put(open_c);
    }

    using ElementReference = decltype(*std::begin(fv.range));
    using ElemType = typename std::remove_cv<
        typename std::remove_reference<ElementReference>::type>::type;
    formatter<ElemType> elem_fmt;
    format_parse_context elem_ctx(forwarded_spec);
    elem_fmt.parse(elem_ctx);

    size_t matched_count = 0;
    for (const auto &item : fv.range) {
      if (fv.predicate(item)) {
        if (matched_count++ > 0) {
          out.write(fv.separator);
        }
        elem_fmt.format(item, out);
      }
    }

    if (close_c != '\0') {
      out.put(close_c);
    }
  }
};

/** Formatter for @ref filtered_pointer_range_view.
 *
 *  Its specifier behavior is identical to @ref filtered_range_view. `b`
 *  writes square brackets, `c` writes braces, and `n` omits outer delimiters.
 */
template <typename T, typename Predicate>
struct formatter<filtered_pointer_range_view<T, Predicate>> {
  char open_c{'['};
  char close_c{']'};
  microfmt::string_view forwarded_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    if (spec.empty())
      return;

    size_t i = 0;
    if (spec[0] == 'b' || spec[0] == 'B') {
      open_c = '[';
      close_c = ']';
      i = 1;
    } else if (spec[0] == 'c' || spec[0] == 'C') {
      open_c = '{';
      close_c = '}';
      i = 1;
    } else if (spec[0] == 'n' || spec[0] == 'N') {
      open_c = '\0';
      close_c = '\0';
      i = 1;
    }
    forwarded_spec = spec.substr(i);
  }

  void format(const filtered_pointer_range_view<T, Predicate> &fv,
              const sink &out) const noexcept {
    if (open_c != '\0') {
      out.put(open_c);
    }

    formatter<T> elem_fmt;
    format_parse_context elem_ctx(forwarded_spec);
    elem_fmt.parse(elem_ctx);

    size_t matched_count = 0;
    for (size_t i = 0; i < fv.count; ++i) {
      const T &item = fv.data[i];
      if (fv.predicate(item)) {
        if (matched_count++ > 0) {
          out.write(fv.separator);
        }
        elem_fmt.format(item, out);
      }
    }

    if (close_c != '\0') {
      out.put(close_c);
    }
  }
};

} // namespace microfmt