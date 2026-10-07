// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

// std-interop-file: opt-in sink/formatting adapters defaulting to std::string/std::vector containers.

/** @file container_sink.hpp @brief Growable character-container sink adapter. */

#include "../microfmt.hpp"
#include <reloco/allocator.hpp>
#include <reloco/error.hpp>
#include <cstddef>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace microfmt {

namespace detail {

template <typename Container, typename = void> struct has_data_size_impl : std::false_type {};

template <typename Container>
struct has_data_size_impl<Container, std::void_t<decltype(std::declval<Container &>().data()),
                                                 decltype(std::declval<Container &>().size())>>
    : std::integral_constant<
          bool, std::is_convertible<decltype(std::declval<Container &>().data()), const char *>::value &&
                    std::is_convertible<decltype(std::declval<Container &>().size()), std::size_t>::value> {};

template <typename Container, typename = void> struct has_std_push_back : std::false_type {};

template <typename Container>
struct has_std_push_back<Container, std::void_t<decltype(std::declval<Container &>().push_back(char{}))>>
    : std::true_type {};

// Fallible (reloco-style) containers report allocation failure through a result instead of throwing.
template <typename Container, typename = void> struct has_try_push_back : std::false_type {};

template <typename Container>
struct has_try_push_back<Container, std::void_t<decltype(static_cast<bool>(std::declval<Container &>().try_push_back(char{})))>>
    : std::true_type {};

template <typename Container, typename = void> struct has_try_append : std::false_type {};

template <typename Container>
struct has_try_append<Container, std::void_t<decltype(static_cast<bool>(
                                     std::declval<Container &>().try_append(microfmt::string_view{})))>>
    : std::true_type {};

template <typename Container>
struct is_growable_char_container_impl
    : std::integral_constant<bool, has_data_size_impl<Container>::value &&
                                       (has_std_push_back<Container>::value || has_try_push_back<Container>::value)> {};

template <typename Container, typename = void> struct has_append : std::false_type {};

template <typename Container>
struct has_append<Container, std::void_t<decltype(std::declval<Container &>().append(
                                 std::declval<const char *>(), std::declval<std::size_t>()))>> : std::true_type {};

template <typename Container, typename = void> struct has_range_insert : std::false_type {};

template <typename Container>
struct has_range_insert<
    Container, std::void_t<decltype(std::declval<Container &>().insert(
                   std::declval<Container &>().end(), std::declval<const char *>(), std::declval<const char *>()))>>
    : std::true_type {};

} // namespace detail

template <typename Container>
inline constexpr bool is_growable_char_container = detail::is_growable_char_container_impl<Container>::value;

// ============================================================================
// Dynamic Container Sink Adapter
// ============================================================================

template <typename Container, typename std::enable_if<is_growable_char_container<Container>, int>::type = 0>
class RELOCO_POINTER container_sink {
public:
  explicit constexpr container_sink(Container &target RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : target_(&target) {}

  container_sink(const container_sink &) = delete;
  container_sink &operator=(const container_sink &) = delete;
  container_sink(container_sink &&) noexcept = default;
  container_sink &operator=(container_sink &&) noexcept = default;

  [[nodiscard]] sink as_sink() noexcept RELOCO_LIFETIMEBOUND {
    return sink{this,
                [](void *ctx, microfmt::string_view sv) noexcept { static_cast<container_sink *>(ctx)->write(sv); }};
  }

  void put(char c) { write(microfmt::string_view(&c, 1)); }

  /// True if the container failed to grow (reloco `try_*` error, or an exception from a std container
  /// when exceptions are enabled); output was truncated from that point.
  [[nodiscard]] bool failed() const noexcept { return failed_; }

  // Never throws: the sink callback is noexcept, so std container exceptions are caught and recorded.
  void write(microfmt::string_view sv) noexcept {
    if (failed_) {
      return;
    }
    if constexpr (!detail::has_std_push_back<Container>::value) {
      write_fallible(sv);
    } else {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
      try {
        write_std(sv);
      } catch (...) { // std-interop-ok: std containers report allocation failure by throwing
        failed_ = true;
      }
#else
      write_std(sv);
#endif
    }
  }

private:
  void write_fallible(microfmt::string_view sv) noexcept {
    if constexpr (detail::has_try_append<Container>::value) {
      if (!target_->try_append(sv)) {
        failed_ = true;
      }
    } else {
      for (char c : sv) {
        if (!target_->try_push_back(c)) {
          failed_ = true;
          return;
        }
      }
    }
  }

  void write_std(microfmt::string_view sv) {
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

  value_ptr<Container> target_;
  bool failed_ = false;
};

// Helper factory for deduction
template <typename Container, typename std::enable_if<is_growable_char_container<Container>, int>::type = 0>
[[nodiscard]] constexpr auto make_container_sink(Container &c RELOCO_LIFETIMEBOUND) noexcept {
  return container_sink<Container>{c};
}

// ============================================================================
// Direct Append & Allocation Helpers
// ============================================================================

// Appends formatted text to an existing growable character container.
template <typename Container, typename... Args>
typename std::enable_if<is_growable_char_container<Container>, void>::type
format_to_container(Container &dest, microfmt::string_view fmt_str, const Args &...args) {
  container_sink<Container> cs(dest);
  auto out = cs.as_sink();
  format_to(out, fmt_str, args...);
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
  if (cs.failed()) {
    throw std::bad_alloc(); // std-interop-ok: preserve the container's throwing contract
  }
#endif
}

// Returns a newly allocated container.
template <typename Container = std::string, typename... Args>
[[nodiscard]] typename std::enable_if<is_growable_char_container<Container>, Container>::type
format_as_container(microfmt::string_view fmt_str, const Args &...args) {
  Container result;
  format_to_container(result, fmt_str, args...);
  return result;
}

// Formats into a new container bound to `alloc`. Fails with reloco::error::allocation_failed if the
// container could not grow to hold the whole output. Requires an allocator_ref-constructible container
// (e.g. reloco::string, reloco::vector<char>).
template <typename Container, typename... Args>
[[nodiscard]] typename std::enable_if<is_growable_char_container<Container> &&
                                          std::is_constructible<Container, reloco::allocator_ref>::value,
                                      reloco::result<Container>>::type
try_format_as_container(reloco::allocator_ref alloc, microfmt::string_view fmt_str, const Args &...args) {
  Container result(alloc);
  container_sink<Container> cs(result);
  format_to(cs.as_sink(), fmt_str, args...);
  if (cs.failed()) {
    return reloco::unexpected(reloco::error::allocation_failed);
  }
  return result;
}

// Appends to an existing container; the container's own allocator is used.
template <typename Container, typename... Args>
[[nodiscard]] typename std::enable_if<is_growable_char_container<Container>, reloco::result<void>>::type
try_format_to_container(Container &dest, microfmt::string_view fmt_str, const Args &...args) {
  container_sink<Container> cs(dest);
  format_to(cs.as_sink(), fmt_str, args...);
  if (cs.failed()) {
    return reloco::unexpected(reloco::error::allocation_failed);
  }
  return {};
}

} // namespace microfmt