#pragma once

/** @file tuple.hpp
 *  @brief Format std::tuple and other tuple-like values without allocation.
 *
 *  Tuple-like types with @c std::tuple_size and @c std::get support are
 *  rendered in parentheses by default. Use `b`, `c`, `n`, or `p` to select
 *  brackets, braces, no delimiters, or parentheses respectively; the
 *  remainder of the specifier is forwarded to every tuple element.
 */

#include "../microfmt.hpp"
#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace microfmt {

namespace detail {

template <typename T>
using remove_cvref_t =
    typename std::remove_cv<typename std::remove_reference<T>::type>::type;

template <typename T, typename = void> struct has_data : std::false_type {};

template <typename T>
struct has_data<T, std::void_t<decltype(std::declval<T &>().data())>>
    : std::true_type {};

/** C++17-compatible detector for supported tuple-like types.
 *
 *  Data-owning/view types are excluded so string and span-like values retain
 *  their dedicated formatting behavior.
 */
template <typename T, typename = void>
struct is_tuple_like : std::false_type {};

template <typename T>
struct is_tuple_like<
    T, std::void_t<decltype(std::tuple_size<remove_cvref_t<T>>::value)>>
    : std::integral_constant<bool, !has_data<remove_cvref_t<T>>::value> {};

template <typename Tuple, size_t... Is>
void format_tuple_impl(const Tuple &t, const sink &out, std::string_view sep,
                       std::string_view spec,
                       std::index_sequence<Is...>) noexcept {
  size_t idx = 0;
  auto format_elem = [&](const auto &elem) noexcept {
    if (idx++ > 0) {
      out.write(sep);
    }
    using ElemType = remove_cvref_t<decltype(elem)>;
    formatter<ElemType> f;
    format_parse_context ctx(spec);
    f.parse(ctx);
    f.format(elem, out);
  };

  (format_elem(std::get<Is>(t)), ...);
}

} // namespace detail

/** Formatter for tuple-like types.
 *
 *  `b` formats with square brackets, `c` with braces, `n` without
 *  delimiters, and `p` with parentheses. Append an element formatter
 *  specifier, for example `{:b04X}`, to apply it to every element.
 */
template <typename T>
struct formatter<T, std::enable_if_t<detail::is_tuple_like<T>::value>> {
  char open_delim{'('};
  char close_delim{')'};
  std::string_view separator{", "};
  std::string_view forwarded_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    if (spec.empty())
      return;

    size_t i = 0;
    // Check custom delimiter style
    if (spec[0] == 'b' || spec[0] == 'B') { // Bracket style: [a, b, c]
      open_delim = '[';
      close_delim = ']';
      i = 1;
    } else if (spec[0] == 'c' ||
               spec[0] == 'C') { // Curly/Brace style: {a, b, c}
      open_delim = '{';
      close_delim = '}';
      i = 1;
    } else if (spec[0] == 'n' ||
               spec[0] == 'N') { // Naked/Bare: a, b, c (no delimiters)
      open_delim = '\0';
      close_delim = '\0';
      i = 1;
    } else if (spec[0] == 'p' || spec[0] == 'P') { // Standard parens: (a, b, c)
      open_delim = '(';
      close_delim = ')';
      i = 1;
    }

    forwarded_spec = spec.substr(i);
  }

  void format(const T &t, const sink &out) const noexcept {
    if (open_delim != '\0') {
      out.put(open_delim);
    }

    constexpr size_t N = std::tuple_size<detail::remove_cvref_t<T>>::value;
    if constexpr (N > 0) {
      detail::format_tuple_impl(t, out, separator, forwarded_spec,
                                std::make_index_sequence<N>{});
    }

    if (close_delim != '\0') {
      out.put(close_delim);
    }
  }
};

} // namespace microfmt