// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file ranges.hpp @brief Range and iterator-pair joining formatting views. */

#include "../microfmt.hpp"
#include <iterator>
#include <string_view>
#include <type_traits>

namespace microfmt {

namespace detail {

template <typename T, typename = void> struct is_range : std::false_type {};

template <typename T>
struct is_range<
    T, std::void_t<decltype(std::begin(std::declval<const T &>())), decltype(std::end(std::declval<const T &>()))>>
    : std::true_type {};

#if RELOCO_CXX20
// Fixed string helper for compile-time format string & delimiter NTTPs
template <size_t N> struct fixed_string {
  char buf[N + 1]{};
  size_t size{N};

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

  constexpr fixed_string(const char (&str)[N + 1]) noexcept {
    for (size_t i = 0; i < N; ++i) {
      buf[i] = str[i];
    }
    buf[N] = '\0';
  }

  RELOCO_END_UNSAFE_BUFFER_USAGE

  [[nodiscard]] constexpr microfmt::string_view view() const noexcept { return microfmt::string_view(buf, N); }
};

template <size_t N> fixed_string(const char (&)[N]) -> fixed_string<N - 1>;
#endif

} // namespace detail

// ============================================================================
// Join View Adapter (Iterator Pair or Range Reference)
// ============================================================================

template <typename It, typename Sentinel = It> struct join_view {
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
  return join_view<It, Sentinel>{first, last, delimiter};
}

template <typename Range, std::enable_if_t<detail::is_range<Range>::value, int> = 0>
[[nodiscard]] constexpr auto join(const Range &range, microfmt::string_view delimiter = ", ") noexcept {
  using std::begin;
  using std::end;
  return join_view<decltype(begin(range)), decltype(end(range))>{begin(range), end(range), delimiter};
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
  using std::begin;
  using std::end;
  return join_as_view<decltype(begin(range)), decltype(end(range)), Delim, ElemSpec>{begin(range), end(range)};
}

template <detail::fixed_string Delim, detail::fixed_string ElemSpec = "", typename Range,
          std::enable_if_t<
              !std::is_lvalue_reference_v<Range> && detail::is_range<std::remove_reference_t<Range>>::value, int> = 0>
[[nodiscard]] constexpr auto join_as(Range &&) noexcept = delete;

template <detail::fixed_string Delim, detail::fixed_string ElemSpec = "", typename It, typename Sentinel,
          std::enable_if_t<!std::is_convertible_v<Sentinel, microfmt::string_view>, int> = 0>
[[nodiscard]] constexpr auto join_as(It first, Sentinel last) noexcept {
  return join_as_view<It, Sentinel, Delim, ElemSpec>{first, last};
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

    formatter<ValueType> element_fmt;
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

    formatter<ValueType> element_fmt;
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
