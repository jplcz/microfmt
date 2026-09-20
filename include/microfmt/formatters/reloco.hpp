#pragma once
#include <microfmt/microfmt.hpp>
#include <reloco/collection_view.hpp>

namespace microfmt {

/**
 * @brief Core formatter for type-erased collection views.
 *
 * Instantiated exactly once per element type `T`. Because it formats
 * through `collection_view`'s vtable, it natively handles bounds-checking
 * and data access for *any* underlying container without template bloat,
 * reusing the same instruction cache for vector<T>, span<T>, array<T, N>.
 */
template <typename T> struct formatter<reloco::collection_view<T>> {
  using value_type = std::remove_cv_t<T>;

  // Holds the parsed state (e.g., width, hex flags) for the underlying elements
  formatter<value_type> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept {
    // Delegate parsing of the format specifier to the element formatter.
    // e.g., "{:04x}" parsed here will configure `underlying_formatter` to pad hex.
    underlying_formatter.parse(ctx);
  }

  void format(const reloco::collection_view<T> &view, const sink &out) const noexcept {
    out.put('[');

    bool is_first = true;

    // Type-erased visit across the container's elements
    view.for_each([&](const T &elem) noexcept {
      if (!is_first) {
        out.write(", ");
      }
      is_first = false;

      // Delegate formatting to the pre-configured element formatter
      underlying_formatter.format(elem, out);
    });

    out.put(']');
  }
};

/**
 * @brief Explicit adapter to format any reloco-adapted container.
 *
 * @example
 *   reloco::vector<int> my_vec = ...;
 *   microfmt::format("{}", microfmt::as_collection_view(my_vec));
 */
template <typename Container,
          std::enable_if_t<reloco::detail::has_collection_view_traits<std::remove_const_t<Container>>::value, int> = 0>
constexpr auto as_collection_view(Container &c) noexcept {
  using traits = reloco::collection_view_traits<std::remove_const_t<Container>>;
  using element_type = typename traits::element_type;

  // Force a const view for formatting so it can safely bind to both const and non-const containers
  return reloco::collection_view<const element_type>(c);
}

} // namespace microfmt
