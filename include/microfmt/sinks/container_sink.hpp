// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

/** @file container_sink.hpp @brief Growable character-container sink adapter. */

#include "../microfmt.hpp"
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace microfmt {

namespace detail {

template <typename Container, typename = void>
struct is_growable_char_container_impl : std::false_type {};

template <typename Container>
struct is_growable_char_container_impl<
    Container,
    std::void_t<decltype(std::declval<Container &>().push_back(char{})),
                decltype(std::declval<Container &>().data()),
                decltype(std::declval<Container &>().size())>>
    : std::integral_constant<
          bool,
          std::is_convertible<
              decltype(std::declval<Container &>().data()),
              const char *>::value &&
              std::is_convertible<decltype(std::declval<Container &>().size()),
                                  std::size_t>::value> {};

template <typename Container, typename = void>
struct has_append : std::false_type {};

template <typename Container>
struct has_append<
    Container,
    std::void_t<decltype(std::declval<Container &>().append(
        std::declval<const char *>(), std::declval<std::size_t>()))>>
    : std::true_type {};

template <typename Container, typename = void>
struct has_range_insert : std::false_type {};

template <typename Container>
struct has_range_insert<
    Container,
    std::void_t<decltype(std::declval<Container &>().insert(
        std::declval<Container &>().end(), std::declval<const char *>(),
        std::declval<const char *>()))>> : std::true_type {};

} // namespace detail

template <typename Container>
inline constexpr bool is_growable_char_container =
    detail::is_growable_char_container_impl<Container>::value;

// ============================================================================
// Dynamic Container Sink Adapter
// ============================================================================

template <typename Container,
          typename std::enable_if<is_growable_char_container<Container>,
                                  int>::type = 0>
class container_sink {
public:
  explicit constexpr container_sink(Container &target) noexcept
      : target_(&target) {}

  container_sink(const container_sink &) = delete;
  container_sink &operator=(const container_sink &) = delete;
  container_sink(container_sink &&) noexcept = default;
  container_sink &operator=(container_sink &&) noexcept = default;

  [[nodiscard]] sink as_sink() noexcept {
    return sink{this, [](void *ctx, std::string_view sv) noexcept {
                  static_cast<container_sink *>(ctx)->write(sv);
                }};
  }

  void put(char c) { target_->push_back(c); }

  void write(std::string_view sv) {
    if constexpr (detail::has_append<Container>::value) {
      target_->append(sv.data(), sv.size());
    } else if constexpr (detail::has_range_insert<Container>::value) {
      target_->insert(target_->end(), sv.begin(), sv.end());
    } else {
      for (char c : sv) {
        target_->push_back(c);
      }
    }
  }

private:
  Container *target_;
};

// Helper factory for deduction
template <typename Container,
          typename std::enable_if<is_growable_char_container<Container>,
                                  int>::type = 0>
[[nodiscard]] constexpr auto make_container_sink(Container &c) noexcept {
  return container_sink<Container>{c};
}

// ============================================================================
// Direct Append & Allocation Helpers
// ============================================================================

// Appends formatted text to an existing growable character container.
template <typename Container, typename... Args>
typename std::enable_if<is_growable_char_container<Container>, void>::type
format_to_container(Container &dest, std::string_view fmt_str,
                    const Args &...args) {
  container_sink<Container> cs(dest);
  auto out = cs.as_sink();
  format_to(out, fmt_str, args...);
}

// Returns a newly allocated container.
template <typename Container = std::string, typename... Args>
[[nodiscard]] typename std::enable_if<is_growable_char_container<Container>,
                                      Container>::type
format_as_container(std::string_view fmt_str, const Args &...args) {
  Container result;
  format_to_container(result, fmt_str, args...);
  return result;
}

} // namespace microfmt