// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file map_view.hpp
 *  @brief Format map-like ranges with zero-allocation key and value views.
 *
 *  Use @ref microfmt::map_view with standard map containers, iterator ranges,
 *  or custom key/value extractors for intrusive and flat containers. Default
 *  formatting renders `{key: value}` entries. Use `b`, `c`, or `n` to select
 *  brackets, braces, or no outer delimiters; remaining format characters are
 *  forwarded to both keys and values.
 */

#include "../microfmt.hpp"
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

namespace microfmt {

/** Non-owning map range and its key/value extraction configuration. */
template <typename KeyExtractor, typename ValExtractor, typename Iterator,
          typename Sentinel>
struct map_range_view {
  Iterator first;
  Sentinel last;
  KeyExtractor key_fn;
  ValExtractor val_fn;
  std::string_view open_delim{"{"};
  std::string_view close_delim{"}"};
  std::string_view sep{", "};
  std::string_view kv_sep{": "};
};

namespace detail {

template <typename T>
using remove_cvref_map_t =
    typename std::remove_cv<typename std::remove_reference<T>::type>::type;

template <typename T, typename = void>
struct has_first_member : std::false_type {};

template <typename T>
struct has_first_member<
    T, std::void_t<decltype(std::declval<const T &>().first)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_second_member : std::false_type {};

template <typename T>
struct has_second_member<
    T, std::void_t<decltype(std::declval<const T &>().second)>>
    : std::true_type {};

template <typename T, typename = void> struct has_key_method : std::false_type {};

template <typename T>
struct has_key_method<T, std::void_t<decltype(std::declval<const T &>().key())>>
    : std::true_type {};

template <typename T, typename = void>
struct has_value_method : std::false_type {};

template <typename T>
struct has_value_method<
    T, std::void_t<decltype(std::declval<const T &>().value())>>
    : std::true_type {};

template <typename T, typename = void>
struct is_dereferenceable : std::false_type {};

template <typename T>
struct is_dereferenceable<T, std::void_t<decltype(*std::declval<T &>())>>
    : std::true_type {};

} // namespace detail

/** Default key extractor for pair-like and key-bearing values. */
struct default_key_fn {
  template <typename T>
  constexpr decltype(auto) operator()(const T &item) const noexcept {
    if constexpr (detail::has_first_member<T>::value) {
      return (item.first);
    } else if constexpr (detail::has_key_method<T>::value) {
      return item.key();
    } else {
      return (item);
    }
  }
};

/** Default value extractor for pair-like and value-bearing values. */
struct default_val_fn {
  template <typename T>
  constexpr decltype(auto) operator()(const T &item) const noexcept {
    if constexpr (detail::has_second_member<T>::value) {
      return (item.second);
    } else if constexpr (detail::has_value_method<T>::value) {
      return item.value();
    } else {
      return (item);
    }
  }
};

/** Create a view using pair-like or key/value-member extraction. */
template <typename MapContainer>
[[nodiscard]] constexpr auto map_view(const MapContainer &m) noexcept {
  return map_range_view<default_key_fn, default_val_fn, decltype(m.begin()),
                        decltype(m.end())>{m.begin(), m.end(), default_key_fn{},
                                           default_val_fn{}};
}

template <typename MapContainer,
          std::enable_if_t<!std::is_lvalue_reference_v<MapContainer>, int> = 0>
[[nodiscard]] constexpr auto map_view(MapContainer &&) noexcept = delete;

/** Create a view using custom extractors for each range element. */
template <typename MapContainer, typename KeyFn, typename ValFn>
[[nodiscard]] constexpr auto map_view(const MapContainer &m, KeyFn &&kfn,
                                      ValFn &&vfn) noexcept {
  return map_range_view<std::decay_t<KeyFn>, std::decay_t<ValFn>,
                        decltype(m.begin()), decltype(m.end())>{
      m.begin(), m.end(), std::forward<KeyFn>(kfn), std::forward<ValFn>(vfn)};
}

template <typename MapContainer, typename KeyFn, typename ValFn,
          std::enable_if_t<!std::is_lvalue_reference_v<MapContainer>, int> = 0>
[[nodiscard]] constexpr auto map_view(MapContainer &&, KeyFn &&,
                                      ValFn &&) noexcept = delete;

/** Create a view over an iterator/sentinel range with optional extractors. */
template <typename Iterator, typename Sentinel, typename KeyFn = default_key_fn,
          typename ValFn = default_val_fn,
          typename std::enable_if<
              detail::is_dereferenceable<Iterator>::value, int>::type = 0>
[[nodiscard]] constexpr auto map_view(Iterator first, Sentinel last,
                                      KeyFn &&kfn = KeyFn{},
                                      ValFn &&vfn = ValFn{}) noexcept {
  return map_range_view<std::decay_t<KeyFn>, std::decay_t<ValFn>, Iterator,
                        Sentinel>{first, last, std::forward<KeyFn>(kfn),
                                  std::forward<ValFn>(vfn)};
}

/** Formatter for @ref map_range_view.
 *
 *  `b` writes square brackets, `c` writes braces, and `n` omits outer
 *  delimiters. Remaining format characters are applied to each key and value,
 *  for example `{:b04X}`.
 */
template <typename KeyExtractor, typename ValExtractor, typename Iterator,
          typename Sentinel>
struct formatter<
    map_range_view<KeyExtractor, ValExtractor, Iterator, Sentinel>> {
  char open_c{'{'};
  char close_c{'}'};
  std::string_view forwarded_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    if (spec.empty())
      return;

    size_t i = 0;
    if (spec[0] == 'b' || spec[0] == 'B') { // Bracketed: [k: v, ...]
      open_c = '[';
      close_c = ']';
      i = 1;
    } else if (spec[0] == 'n' || spec[0] == 'N') { // Naked: k: v, ...
      open_c = '\0';
      close_c = '\0';
      i = 1;
    } else if (spec[0] == 'c' || spec[0] == 'C') { // Curly: {k: v, ...}
      open_c = '{';
      close_c = '}';
      i = 1;
    }

    forwarded_spec = spec.substr(i);
  }

  void format(const map_range_view<KeyExtractor, ValExtractor, Iterator,
                                   Sentinel> &view,
              const sink &out) const noexcept {
    if (open_c != '\0') {
      out.put(open_c);
    }

    size_t idx = 0;
    for (auto it = view.first; it != view.last; ++it) {
      if (idx++ > 0) {
        out.write(view.sep);
      }

      const auto &k = view.key_fn(*it);
      const auto &v = view.val_fn(*it);

      using KeyType = detail::remove_cvref_map_t<decltype(k)>;
      using ValType = detail::remove_cvref_map_t<decltype(v)>;

      formatter<KeyType> key_fmt;
      formatter<ValType> val_fmt;

      format_parse_context kctx(forwarded_spec);
      key_fmt.parse(kctx);
      key_fmt.format(k, out);

      out.write(view.kv_sep);

      format_parse_context vctx(forwarded_spec);
      val_fmt.parse(vctx);
      val_fmt.format(v, out);
    }

    if (close_c != '\0') {
      out.put(close_c);
    }
  }
};

} // namespace microfmt