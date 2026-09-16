// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file value_ptr.hpp
 * @brief Nullable non-owning pointer wrapper with lifetime annotations. */

#include "lifetime.hpp"
#include <cstddef>
#include <type_traits>

namespace microfmt {

/**
 * @brief Nullable, non-owning pointer to a caller-owned value.
 *
 * `value_ptr` documents retained pointer relationships without taking
 * ownership. The pointed object must outlive the wrapper and every pointer or
 * reference obtained from it.
 */
template <typename T> class MICROFMT_POINTER value_ptr {
public:
  constexpr value_ptr() noexcept = default;
  constexpr value_ptr(std::nullptr_t) noexcept {}

  template <typename U,
            typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
  constexpr value_ptr(U *ptr MICROFMT_LIFETIMEBOUND) noexcept : ptr_(ptr) {}

  template <typename U,
            typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
  constexpr value_ptr(value_ptr<U> other) noexcept : ptr_(other.ptr_) {}

  [[nodiscard]] constexpr T *
  get() const noexcept MICROFMT_LIFETIMEBOUND {
    return ptr_;
  }

  template <typename U = T,
            std::enable_if_t<!std::is_void_v<U>, int> = 0>
  [[nodiscard]] constexpr U &
  operator*() const noexcept MICROFMT_LIFETIMEBOUND {
    return *ptr_;
  }

  [[nodiscard]] constexpr T *
  operator->() const noexcept MICROFMT_LIFETIMEBOUND {
    return ptr_;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return ptr_ != nullptr;
  }

private:
  template <typename> friend class value_ptr;

  T *ptr_{nullptr};
};

template <typename T> value_ptr(T *) -> value_ptr<T>;

} // namespace microfmt
