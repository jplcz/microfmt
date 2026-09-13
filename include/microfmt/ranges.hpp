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

} // namespace detail

// ============================================================================
// Join View Adapter (Iterator Pair or Range Reference)
// ============================================================================

template <typename It, typename Sentinel = It> struct join_view {
  It first;
  Sentinel last;
  std::string_view delimiter{", "};
};

// ============================================================================
// Factory Functions
// ============================================================================

template <typename It, typename Sentinel>
  requires(!std::is_convertible_v<Sentinel, std::string_view>)
[[nodiscard]] constexpr auto join(It first, Sentinel last,
                                  std::string_view delimiter = ", ") noexcept {
  return join_view<It, Sentinel>{first, last, delimiter};
}

// Generic Range / Container overload (C-style arrays, std::array, span,
// etc.)
template <detail::is_range Range>
[[nodiscard]] constexpr auto join(const Range &range,
                                  std::string_view delimiter = ", ") noexcept {
  using std::begin;
  using std::end;
  return join_view<decltype(begin(range)), decltype(end(range))>{
      begin(range), end(range), delimiter};
}

// ============================================================================
// Formatter Specialization for join_view
// ============================================================================

template <typename It, typename Sentinel>
struct formatter<join_view<It, Sentinel>> {
  constexpr void parse(format_parse_context &ctx) noexcept { (void)ctx; }

  void format(const join_view<It, Sentinel> &jv,
              const sink &out) const noexcept {
    using ValueType = std::remove_cv_t<
        std::remove_reference_t<decltype(*std::declval<It>())>>;

    formatter<ValueType> element_fmt;
    format_parse_context dummy_ctx("");
    element_fmt.parse(dummy_ctx);

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

} // namespace microfmt
