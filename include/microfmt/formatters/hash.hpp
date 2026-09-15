// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

/** @file hash.hpp @brief `std::hash`-based non-owning hash formatting views. */

#include "../microfmt.hpp"
#include <cstddef>
#include <functional>
#include <type_traits>

namespace microfmt {

namespace detail {

/**
 * @brief SFINAE trait detecting types invocable through `std::hash<T>`.
 * @tparam T Candidate hashed type.
 */
template <typename T, typename = void> struct is_hashable : std::false_type {};

/**
 * @brief Specialization enabling @ref is_hashable when `std::hash<T>` is
 * callable.
 * @tparam T Candidate hashed type.
 */
template <typename T>
struct is_hashable<
    T, std::void_t<decltype(std::hash<T>{}(std::declval<const T &>()))>>
    : std::true_type {};

/**
 * @brief Convenience variable template for @ref is_hashable.
 * @tparam T Candidate hashed type.
 */
template <typename T>
inline constexpr bool is_hashable_v = is_hashable<T>::value;

} // namespace detail

// ============================================================================
// Non-Owning Hash View
// ============================================================================

/**
 * @brief Non-owning view that defers a value's hash computation to formatting
 * time.
 *
 * @tparam T Hashable value type.
 */
template <typename T> class hash_view {
public:
  /**
   * @brief Constructs a hash view referencing an existing value.
   * @param val Value whose `std::hash` result will be formatted.
   */
  constexpr explicit hash_view(const T &val) noexcept : val_(val) {}

  /**
   * @brief Computes the hash of the referenced value.
   * @return Result of `std::hash<T>{}` applied to the stored value.
   */
  [[nodiscard]] std::size_t compute_hash() const noexcept {
    return std::hash<T>{}(val_);
  }

private:
  /// Referenced value.
  const T &val_;
};

// ============================================================================
// Factory Function: microfmt::as_hash(...)
// ============================================================================

/**
 * @brief Wraps a value in an @ref hash_view for hashed formatting.
 * @tparam T Hashable type, enabled via @ref detail::is_hashable_v.
 * @param val Value whose hash should be formatted.
 * @return A formattable @ref hash_view over @p val.
 */
template <typename T, std::enable_if_t<detail::is_hashable_v<T>, int> = 0>
[[nodiscard]] constexpr auto as_hash(const T &val) noexcept {
  return hash_view<T>{val};
}

// ============================================================================
// Formatter for hash_view
// ============================================================================

/**
 * @brief Formatter for @ref hash_view.
 *
 * Forwards any format specifier to the wrapped `std::size_t` formatter.
 *
 * @tparam T Holds the wrapped integral hash type (kept as the view template
 * parameter).
 */
template <typename T> struct formatter<hash_view<T>> {
  /**
   * @brief Underlying formatter for the computed hash value.
   */
  formatter<std::size_t> int_fmt_{};

  /**
   * @brief Parses the specifier for the wrapped integer formatter.
   * @param ctx Format parse context exposing the specifier text.
   */
  constexpr void parse(format_parse_context &ctx) noexcept {
    int_fmt_.parse(ctx);
  }

  /**
   * @brief Computes and renders the hash value.
   * @param view The hash view to format.
   * @param out Destination sink.
   */
  void format(const hash_view<T> &view, const sink &out) const noexcept {
    int_fmt_.format(view.compute_hash(), out);
  }
};

} // namespace microfmt