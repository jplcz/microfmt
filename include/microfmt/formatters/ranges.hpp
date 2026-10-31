// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file ranges.hpp @brief Range and iterator-pair joining formatting views. */

#include "../microfmt.hpp"
#include <iterator>
#include <utility>
#include <string_view>
#include <type_traits>

namespace microfmt {

namespace detail {

template <typename T, typename = void> struct is_range : std::false_type {};

template <typename T>
struct is_range<
    T, std::void_t<decltype(std::begin(std::declval<const T &>())), decltype(std::end(std::declval<const T &>()))>>
    : std::true_type {};

// Contiguous ranges (C arrays, std::array, std::vector, std::span, ...) and raw pointer pairs are
// normalised to a `const T *` pair so every iterator flavour over the same element type shares one
// join_view/formatter instantiation instead of one per iterator type.
template <typename T, typename = void> struct has_data_and_size : std::false_type {};
template <typename T>
struct has_data_and_size<T, std::void_t<decltype(std::data(std::declval<const T &>())),
                                        decltype(std::size(std::declval<const T &>()))>> : std::true_type {};

template <typename Range> constexpr auto join_bounds(const Range &range) noexcept {
  using std::begin;
  using std::end;
  if constexpr (has_data_and_size<Range>::value) {
    using Elem = std::remove_cv_t<std::remove_pointer_t<decltype(std::data(range))>>;
    using Deref = std::remove_cv_t<std::remove_reference_t<decltype(*begin(range))>>;
    if constexpr (std::is_same_v<Elem, Deref>) {
      const Elem *first = std::data(range);
      RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
      const Elem *last = first + std::size(range);
      RELOCO_END_UNSAFE_BUFFER_USAGE;
      return std::pair<const Elem *, const Elem *>{first, last};
    } else {
      return std::pair{begin(range), end(range)};
    }
  } else {
    return std::pair{begin(range), end(range)};
  }
}

template <typename It, typename Sentinel> constexpr auto join_bounds(It first, Sentinel last) noexcept {
  if constexpr (std::is_pointer_v<It> && std::is_pointer_v<Sentinel> &&
                std::is_same_v<std::remove_cv_t<std::remove_pointer_t<It>>,
                               std::remove_cv_t<std::remove_pointer_t<Sentinel>>>) {
    using Elem = std::remove_cv_t<std::remove_pointer_t<It>>;
    return std::pair<const Elem *, const Elem *>{first, last};
  } else {
    return std::pair<It, Sentinel>{first, last};
  }
}

#if RELOCO_CXX20
// Fixed string helper for compile-time format string & delimiter NTTPs
template <size_t N> struct fixed_string {
  char buf[N + 1]{};
  size_t size{N};

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

  // Implicit on purpose: a string literal must convert to the class-type NTTP (`join_as<", ">(r)`).
  constexpr fixed_string(const char (&str)[N + 1]) noexcept { // NOLINT(reloco-explicit-conversion)
    for (size_t i = 0; i < N; ++i) {
      buf[i] = str[i];
    }
    buf[N] = '\0';
  }

  RELOCO_END_UNSAFE_BUFFER_USAGE

  [[nodiscard]] constexpr microfmt::string_view view() const & noexcept RELOCO_LIFETIMEBOUND {
    return microfmt::string_view(buf, N);
  }

  [[nodiscard]] constexpr microfmt::string_view view() const && = delete;
};

template <size_t N> fixed_string(const char (&)[N]) -> fixed_string<N - 1>;
#endif

} // namespace detail

// ============================================================================
// Join View Adapter (Iterator Pair or Range Reference)
// ============================================================================

template <typename It, typename Sentinel = It> struct RELOCO_POINTER join_view {
  It first;
  Sentinel last;
  microfmt::string_view delimiter{", "};
  // Custom per-element specifier (e.g. "02x")
};

#if RELOCO_CXX20
// ============================================================================
// Zero-Size Specifier join_as_view (Compile-Time NTTPs)
// ============================================================================

template <typename It, typename Sentinel, detail::fixed_string Delim, detail::fixed_string ElemSpec>
struct join_as_view {
  It first;
  Sentinel last;
};
#endif

// ============================================================================
// Factory Functions
// ============================================================================

// Standard join view
template <typename It, typename Sentinel,
          std::enable_if_t<!std::is_convertible_v<Sentinel, microfmt::string_view>, int> = 0>
[[nodiscard]] constexpr auto join(It first, Sentinel last, microfmt::string_view delimiter = ", ") noexcept {
  const auto [b, e] = detail::join_bounds(first, last);
  return join_view<std::remove_cv_t<decltype(b)>, std::remove_cv_t<decltype(e)>>{b, e, delimiter};
}

template <typename Range, std::enable_if_t<detail::is_range<Range>::value, int> = 0>
[[nodiscard]] constexpr auto join(const Range &range, microfmt::string_view delimiter = ", ") noexcept {
  const auto [b, e] = detail::join_bounds(range);
  return join_view<std::remove_cv_t<decltype(b)>, std::remove_cv_t<decltype(e)>>{b, e, delimiter};
}

template <typename Range,
          std::enable_if_t<
              !std::is_lvalue_reference_v<Range> && detail::is_range<std::remove_reference_t<Range>>::value, int> = 0>
[[nodiscard]] constexpr auto join(Range &&, microfmt::string_view = ", ") noexcept = delete;

#if RELOCO_CXX20
// Compile-time join_as (zero runtime overhead)
template <detail::fixed_string Delim, detail::fixed_string ElemSpec = "", typename Range,
          std::enable_if_t<detail::is_range<Range>::value, int> = 0>
[[nodiscard]] constexpr auto join_as(const Range &range) noexcept {
  const auto [b, e] = detail::join_bounds(range);
  return join_as_view<std::remove_cv_t<decltype(b)>, std::remove_cv_t<decltype(e)>, Delim, ElemSpec>{b, e};
}

template <detail::fixed_string Delim, detail::fixed_string ElemSpec = "", typename Range,
          std::enable_if_t<
              !std::is_lvalue_reference_v<Range> && detail::is_range<std::remove_reference_t<Range>>::value, int> = 0>
[[nodiscard]] constexpr auto join_as(Range &&) noexcept = delete;

template <detail::fixed_string Delim, detail::fixed_string ElemSpec = "", typename It, typename Sentinel,
          std::enable_if_t<!std::is_convertible_v<Sentinel, microfmt::string_view>, int> = 0>
[[nodiscard]] constexpr auto join_as(It first, Sentinel last) noexcept {
  const auto [b, e] = detail::join_bounds(first, last);
  return join_as_view<std::remove_cv_t<decltype(b)>, std::remove_cv_t<decltype(e)>, Delim, ElemSpec>{b, e};
}
#endif

// ============================================================================
// Formatter for join_view (Receives element spec from format string {:02x})
// ============================================================================

template <typename It, typename Sentinel> struct formatter<join_view<It, Sentinel>> {
  microfmt::string_view elem_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept { elem_spec = ctx.spec(); }

  void format(const join_view<It, Sentinel> &jv, const sink &out) const noexcept {
    using ValueType = std::remove_cv_t<std::remove_reference_t<decltype(*std::declval<It>())>>;

    detail::element_formatter<ValueType> element_fmt;
    format_parse_context elem_ctx(elem_spec);
    element_fmt.parse(elem_ctx);

    bool is_first = true;
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

    for (auto it = jv.first; it != jv.last; ++it) {
      if (!is_first) {
        out.write(jv.delimiter);
      }
      is_first = false;
      element_fmt.format(*it, out);
    }
    RELOCO_END_UNSAFE_BUFFER_USAGE;
  }
};

#if RELOCO_CXX20
// ============================================================================
// Formatter for join_as_view (Zero runtime state overhead)
// ============================================================================

template <typename It, typename Sentinel, detail::fixed_string Delim, detail::fixed_string ElemSpec>
struct formatter<join_as_view<It, Sentinel, Delim, ElemSpec>> {
  microfmt::string_view runtime_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept { runtime_spec = ctx.spec(); }

  void format(const join_as_view<It, Sentinel, Delim, ElemSpec> &jv, const sink &out) const noexcept {
    using ValueType = std::remove_cv_t<std::remove_reference_t<decltype(*std::declval<It>())>>;

    constexpr microfmt::string_view ct_elem_spec = ElemSpec.view();
    const microfmt::string_view effective_spec = !ct_elem_spec.empty() ? ct_elem_spec : runtime_spec;

    detail::element_formatter<ValueType> element_fmt;
    format_parse_context elem_ctx(effective_spec);
    element_fmt.parse(elem_ctx);

    bool is_first = true;
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

    for (auto it = jv.first; it != jv.last; ++it) {
      if (!is_first) {
        out.write(Delim.view());
      }
      is_first = false;
      element_fmt.format(*it, out);
    }

    RELOCO_END_UNSAFE_BUFFER_USAGE;
  }
};
#endif

} // namespace microfmt
