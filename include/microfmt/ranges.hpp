#pragma once

#include "microfmt.hpp"
#include <iterator>
#include <string_view>
#include <type_traits>

namespace microfmt {

namespace detail {

// Range detection concept
template <typename T>
concept is_range = requires(T &t) {
  std::begin(t);
  std::end(t);
};

// Fixed string helper for compile-time format string & delimiter NTTPs
template <size_t N> struct fixed_string {
  char buf[N + 1]{};
  size_t size{N};

  constexpr fixed_string(const char (&str)[N + 1]) noexcept {
    for (size_t i = 0; i < N; ++i) {
      buf[i] = str[i];
    }
    buf[N] = '\0';
  }

  [[nodiscard]] constexpr std::string_view view() const noexcept {
    return std::string_view(buf, N);
  }
};

template <size_t N> fixed_string(const char (&)[N]) -> fixed_string<N - 1>;

} // namespace detail

// ============================================================================
// Join View Adapter (Iterator Pair or Range Reference)
// ============================================================================

template <typename It, typename Sentinel = It> struct join_view {
  It first;
  Sentinel last;
  std::string_view delimiter{", "};
  // Custom per-element specifier (e.g. "02x")
};

// ============================================================================
// Zero-Size Specifier join_as_view (Compile-Time NTTPs)
// ============================================================================

template <typename It, typename Sentinel, detail::fixed_string Delim,
          detail::fixed_string ElemSpec>
struct join_as_view {
  It first;
  Sentinel last;
};

// ============================================================================
// Factory Functions
// ============================================================================

// Standard join view
template <typename It, typename Sentinel>
  requires(!std::is_convertible_v<Sentinel, std::string_view>)
[[nodiscard]] constexpr auto join(It first, Sentinel last,
                                  std::string_view delimiter = ", ") noexcept {
  return join_view<It, Sentinel>{first, last, delimiter};
}

template <detail::is_range Range>
[[nodiscard]] constexpr auto join(const Range &range,
                                  std::string_view delimiter = ", ") noexcept {
  using std::begin;
  using std::end;
  return join_view<decltype(begin(range)), decltype(end(range))>{
      begin(range), end(range), delimiter};
}

// Compile-time join_as (zero runtime overhead)
template <detail::fixed_string Delim, detail::fixed_string ElemSpec = "",
          detail::is_range Range>
[[nodiscard]] constexpr auto join_as(const Range &range) noexcept {
  using std::begin;
  using std::end;
  return join_as_view<decltype(begin(range)), decltype(end(range)), Delim,
                      ElemSpec>{begin(range), end(range)};
}

template <detail::fixed_string Delim, detail::fixed_string ElemSpec = "",
          typename It, typename Sentinel>
  requires(!std::is_convertible_v<Sentinel, std::string_view>)
[[nodiscard]] constexpr auto join_as(It first, Sentinel last) noexcept {
  return join_as_view<It, Sentinel, Delim, ElemSpec>{first, last};
}

// ============================================================================
// Formatter for join_view (Receives element spec from format string {:02x})
// ============================================================================

template <typename It, typename Sentinel>
struct formatter<join_view<It, Sentinel>> {
  std::string_view elem_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept {
    elem_spec = ctx.spec();
  }

  void format(const join_view<It, Sentinel> &jv,
              const sink &out) const noexcept {
    using ValueType = std::remove_cv_t<
        std::remove_reference_t<decltype(*std::declval<It>())>>;

    formatter<ValueType> element_fmt;
    format_parse_context elem_ctx(elem_spec);
    element_fmt.parse(elem_ctx);

    bool is_first = true;
    for (auto it = jv.first; it != jv.last; ++it) {
      if (!is_first) {
        out.write(jv.delimiter);
      }
      is_first = false;
      element_fmt.format(*it, out);
    }
  }
};

// ============================================================================
// Formatter for join_as_view (Zero runtime state overhead)
// ============================================================================

template <typename It, typename Sentinel, detail::fixed_string Delim,
          detail::fixed_string ElemSpec>
struct formatter<join_as_view<It, Sentinel, Delim, ElemSpec>> {
  std::string_view runtime_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept {
    runtime_spec = ctx.spec();
  }

  void format(const join_as_view<It, Sentinel, Delim, ElemSpec> &jv,
              const sink &out) const noexcept {
    using ValueType = std::remove_cv_t<
        std::remove_reference_t<decltype(*std::declval<It>())>>;

    constexpr std::string_view ct_elem_spec = ElemSpec.view();
    const std::string_view effective_spec =
        !ct_elem_spec.empty() ? ct_elem_spec : runtime_spec;

    formatter<ValueType> element_fmt;
    format_parse_context elem_ctx(effective_spec);
    element_fmt.parse(elem_ctx);

    bool is_first = true;
    for (auto it = jv.first; it != jv.last; ++it) {
      if (!is_first) {
        out.write(Delim.view());
      }
      is_first = false;
      element_fmt.format(*it, out);
    }
  }
};

} // namespace microfmt
