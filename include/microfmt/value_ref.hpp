// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file value_ref.hpp
 * @brief Safe read-only reference wrapper rejecting rvalues and temporaries. */

#include "lifetime.hpp"
#include "value_ptr.hpp"
#include <memory>
#include <type_traits>
#include <utility>

namespace microfmt {

/**
 * @brief Type-safe read-only reference wrapper that rejects temporary objects.
 *
 * The referenced object must outlive the wrapper. Use @ref value_ptr when the
 * borrow must be nullable or provide mutable access.
 */
template <typename T> class MICROFMT_POINTER value_ref {
public:
  /**
   * @brief Constructs a reference wrapper from a persistent lvalue reference.
   * @param val Stable, non-temporary object.
   */
  template <typename U,
            typename =
                std::enable_if_t<std::is_convertible_v<U *, const T *>>>
  constexpr explicit value_ref(
      U &val MICROFMT_LIFETIMEBOUND
          MICROFMT_LIFETIME_CAPTURE_BY_THIS) noexcept
      : m_ptr(std::addressof(val)) {}

  /**
   * @brief Rejects rvalue and temporary object bindings.
   */
  template <typename U,
            std::enable_if_t<!std::is_lvalue_reference_v<U>, int> = 0>
  constexpr value_ref(U &&) = delete;

  [[nodiscard]] constexpr const T *
  get() const noexcept MICROFMT_LIFETIMEBOUND {
    return m_ptr.get();
  }

  [[nodiscard]] constexpr const T &
  operator*() const noexcept MICROFMT_LIFETIMEBOUND {
    return *m_ptr;
  }

  [[nodiscard]] constexpr const T *
  operator->() const noexcept MICROFMT_LIFETIMEBOUND {
    return m_ptr.get();
  }

private:
  value_ptr<const T> m_ptr;
};

template <typename T> value_ref(T &) -> value_ref<T>;

} // namespace microfmt
