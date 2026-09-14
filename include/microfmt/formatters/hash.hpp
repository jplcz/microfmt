// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "../microfmt.hpp"
#include <cstddef>
#include <functional>
#include <type_traits>

namespace microfmt {

namespace detail {

template <typename T, typename = void> struct is_hashable : std::false_type {};

template <typename T>
struct is_hashable<
    T, std::void_t<decltype(std::hash<T>{}(std::declval<const T &>()))>>
    : std::true_type {};

template <typename T>
inline constexpr bool is_hashable_v = is_hashable<T>::value;

} // namespace detail

// ============================================================================
// Non-Owning Hash View
// ============================================================================

template <typename T> class hash_view {
public:
  constexpr explicit hash_view(const T &val) noexcept : val_(val) {}

  [[nodiscard]] std::size_t compute_hash() const noexcept {
    return std::hash<T>{}(val_);
  }

private:
  const T &val_;
};

// ============================================================================
// Factory Function: microfmt::as_hash(...)
// ============================================================================

template <typename T, std::enable_if_t<detail::is_hashable_v<T>, int> = 0>
[[nodiscard]] constexpr auto as_hash(const T &val) noexcept {
  return hash_view<T>{val};
}

// ============================================================================
// Formatter for hash_view
// ============================================================================

template <typename T> struct formatter<hash_view<T>> {
  formatter<std::size_t> int_fmt_{};

  constexpr void parse(format_parse_context &ctx) noexcept {
    int_fmt_.parse(ctx);
  }

  void format(const hash_view<T> &view, const sink &out) const noexcept {
    int_fmt_.format(view.compute_hash(), out);
  }
};

} // namespace microfmt