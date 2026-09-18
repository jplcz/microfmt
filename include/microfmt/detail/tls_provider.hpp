// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file tls_provider.hpp @brief Tag-differentiated thread-local/task-local storage provider. */

namespace microfmt::detail {

/**
 * @brief Default thread-local storage provider differentiated by T and Tag.
 * @tparam T The type of object being stored per thread/task.
 * @tparam Tag A unique tag type used to separate distinct instances of type T.
 */
template <typename T, typename Tag> struct default_tls_provider {
  [[nodiscard]] static inline T *get() noexcept { return instance_; }

  static inline void set(T *ptr) noexcept { instance_ = ptr; }

private:
  static inline thread_local T *instance_ = nullptr;
};

} // namespace microfmt::detail

/**
 * @def MICROFMT_TLS_PROVIDER
 * @brief Tag-aware customization macro for thread-local state mapping.
 *
 * Allows users to map any state type `T` and unique `Tag` to custom RTOS task
 * control blocks or POSIX thread-specific keys.
 *
 * @code
 *   template <typename T, typename Tag>
 *   struct my_custom_tls_provider {
 *       static T* get() noexcept { return ...; }
 *       static void set(T* ptr) noexcept { ...; }
 *   };
 *   #define MICROFMT_TLS_PROVIDER(T, Tag) my_custom_tls_provider<T, Tag>
 * @code
 */
#ifndef MICROFMT_TLS_PROVIDER
#define MICROFMT_TLS_PROVIDER(T, Tag) ::microfmt::detail::default_tls_provider<T, Tag>
#endif

namespace microfmt {

/**
 * @brief Helper to access thread-local/task-local state of type T differentiated by Tag.
 * @tparam T The state type managed by the active provider.
 * @tparam Tag Unique tag distinguishing this instance from others of type T.
 */
template <typename T, typename Tag> class tls_state {
public:
  using provider = MICROFMT_TLS_PROVIDER(T, Tag);

  /**
   * @brief Gets the current thread/task-local instance pointer.
   */
  [[nodiscard]] static inline T *get() noexcept { return provider::get(); }

  /**
   * @brief Sets the current thread/task-local instance pointer.
   */
  static inline void set(T *ptr) noexcept { provider::set(ptr); }
};

} // namespace microfmt