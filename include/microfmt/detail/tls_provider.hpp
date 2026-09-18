// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once
#include "assert.hpp"

/**
 * @file tls_provider.hpp
 * @brief Tag-differentiated thread-local storage provider framework.
 *
 * Provides a highly modular, multi-model thread-local storage (TLS) abstraction layer
 * designed for freestanding, bare-metal, and cross-platform environments. Storage is
 * uniquely differentiated by both the stored type @p T and a unique type @p Tag.
 */

/** @name Thread-Local Storage Models */
/// @{
#define MICROFMT_TLS_MODEL_THREAD_LOCAL 0 ///< Standard C++20 `thread_local` storage model.
#define MICROFMT_TLS_MODEL_PTHREAD 1      ///< POSIX threads (pthreads) key-based TLS model.
#define MICROFMT_TLS_MODEL_OS 2           ///< Custom OS-specific or bare-metal TCB mapping model.
#define MICROFMT_TLS_MODEL_SINGLE 3       ///< Single-threaded fallback model (global static instance).
#define MICROFMT_TLS_MODEL_WIN32 4        ///< Win32 Fiber Local Storage (FLS) model with auto-cleanup.
/// @}

#if !defined(MICROFMT_TLS_MODEL)
#define MICROFMT_TLS_MODEL MICROFMT_TLS_MODEL_PTHREAD
#endif

#if (MICROFMT_TLS_MODEL == MICROFMT_TLS_MODEL_PTHREAD)
#include <pthread.h>
#endif

#if (MICROFMT_TLS_MODEL == MICROFMT_TLS_MODEL_WIN32)
#include <windows.h>
#endif

#include <cstring>
#include <type_traits>
#include <utility>

namespace microfmt::detail {

#if (MICROFMT_TLS_MODEL == MICROFMT_TLS_MODEL_THREAD_LOCAL)
/**
 * @brief C++ `thread_local` storage provider implementation.
 * @tparam T The type of object being stored.
 * @tparam Tag Unique tag distinguishing this storage instance from others of type T.
 */
template <typename T, typename Tag> struct tls_provider {
  /**
   * @brief Gets a reference to the thread-local instance.
   * @return Reference to the thread-local object.
   */
  [[nodiscard]] static inline T &get() noexcept { return instance_; }

  /**
   * @brief Sets the thread-local instance value.
   * @param value The value to assign.
   */
  static inline void set(T value) noexcept { instance_ = std::move(value); }

private:
  static inline thread_local T instance_ = {};
};
#elif (MICROFMT_TLS_MODEL == MICROFMT_TLS_MODEL_PTHREAD)
struct pt_helpers {
  static void create(pthread_key_t &key, void (*deleter)(void *)) noexcept {
    const int rc = pthread_key_create(&key, deleter);
    if (rc != 0) {
      MICROFMT_TRAP();
    }
  }
  static void *get(pthread_key_t key) noexcept { return pthread_getspecific(key); }
  static void set(pthread_key_t key, void *p) noexcept {
    const int rc = pthread_setspecific(key, p);
    if (rc != 0) {
      MICROFMT_TRAP();
    }
  }
  template <typename T> static void default_deleter(void *p) noexcept { delete static_cast<T *>(p); }
};

template <typename U>
constexpr inline bool can_fit_in_pointer_v = std::is_trivial_v<U> && (sizeof(U) <= sizeof(void *));

template <typename T, typename Tag, typename Enable = void> struct tls_provider;

/// Specialization for Raw Pointers (T*)
template <typename T, typename Tag, typename Enable> struct tls_provider<T *, Tag, Enable> {
  [[nodiscard]] static inline T *get() noexcept {
    init_once();
    return static_cast<T *>(pt_helpers::get(key_));
  }

  /**
   * @brief Sets or updates the thread-local instance value.
   * @param value The value to assign.
   */
  static inline void set(T *value) noexcept {
    init_once();
    pt_helpers::set(key_, value);
  }

private:
  /** @brief Key creation routine executed once. */
  static void init_routine() noexcept { pt_helpers::create(key_, nullptr); }

  /** @brief Ensures thread-safe one-time initialization of the pthread key. */
  static void init_once() noexcept { pthread_once(&key_init_, init_routine); }

  static inline pthread_once_t key_init_ = PTHREAD_ONCE_INIT;
  static inline pthread_key_t key_ = 0;
};

/// Specialization for Heavy / Non-Trivial Values (Heap Allocated with Cleanup)
template <typename T, typename Tag>
struct tls_provider<T, Tag, std::enable_if_t<!std::is_pointer_v<T> && !can_fit_in_pointer_v<T>>> {
  /**
   * @brief Gets a reference to the thread-local instance, allocating it lazily if necessary.
   * @return Reference to the thread-local object.
   */
  [[nodiscard]] static inline T &get() noexcept {
    init_once();
    void *ptr = pt_helpers::get(key_);
    if (!ptr) {
      ptr = new T();
      MICROFMT_ASSERT(ptr != nullptr, "Object not created ?");
      pt_helpers::set(key_, ptr);
    }
    return *static_cast<T *>(ptr);
  }

  /**
   * @brief Sets or updates the thread-local instance value.
   * @param value The value to assign.
   */
  static inline void set(T value) noexcept {
    init_once();
    void *ptr = pt_helpers::get(key_);
    if (!ptr) {
      ptr = new T(std::move(value));
      MICROFMT_ASSERT(ptr != nullptr, "Object not created ?");
      pt_helpers::set(key_, ptr);
    } else {
      *static_cast<T *>(ptr) = std::move(value);
    }
  }

private:
  /** @brief Key creation routine executed once. */
  static void init_routine() noexcept { pt_helpers::create(key_, &pt_helpers::default_deleter<T>); }

  /** @brief Ensures thread-safe one-time initialization of the pthread key. */
  static void init_once() noexcept { pthread_once(&key_init_, init_routine); }

  static inline pthread_once_t key_init_ = PTHREAD_ONCE_INIT;
  static inline pthread_key_t key_ = 0;
};

/// Specialization for Small Trivial Values (Zero Heap Allocation)
template <typename T, typename Tag>
struct tls_provider<T, Tag, std::enable_if_t<!std::is_pointer_v<T> && can_fit_in_pointer_v<T>>> {
  [[nodiscard]] static inline T get() noexcept {
    init_once();
    T value{};
    void *ptr = pt_helpers::get(key_);
    memcpy(&value, &ptr, sizeof(T));
    return value;
  }

  /**
   * @brief Sets or updates the thread-local instance value.
   * @param value The value to assign.
   */
  static inline void set(T value) noexcept {
    init_once();
    void *ptr = nullptr;
    memcpy(&ptr, &value, sizeof(T));
    pt_helpers::set(key_, ptr);
  }

private:
  /** @brief Key creation routine executed once. */
  static void init_routine() noexcept { pt_helpers::create(key_, nullptr); }

  /** @brief Ensures thread-safe one-time initialization of the pthread key. */
  static void init_once() noexcept { pthread_once(&key_init_, init_routine); }

  static inline pthread_once_t key_init_ = PTHREAD_ONCE_INIT;
  static inline pthread_key_t key_ = 0;
};

#elif (MICROFMT_TLS_MODEL == MICROFMT_TLS_MODEL_OS)
/**
 * @brief Custom OS-specific or bare-metal storage provider stub.
 * @tparam T The type of object being stored.
 * @tparam Tag Unique tag distinguishing this storage instance.
 */
template <typename T, typename Tag> struct tls_provider {
  /** @brief Gets a reference to the task/thread-local instance (implemented by target OS/kernel). */
  [[nodiscard]] static T &get() noexcept;
  /** @brief Sets the task/thread-local instance value (implemented by target OS/kernel). */
  static void set(T value) noexcept;
};
#elif (MICROFMT_TLS_MODEL == MICROFMT_TLS_MODEL_SINGLE)
/**
 * @brief Single-threaded fallback storage provider (global static instance).
 * @tparam T The type of object being stored.
 * @tparam Tag Unique tag distinguishing this storage instance.
 */
template <typename T, typename Tag> struct tls_provider {
  /**
   * @brief Gets a reference to the global static instance.
   * @return Reference to the instance.
   */
  [[nodiscard]] static inline T &get() noexcept { return instance_; }

  /**
   * @brief Sets the global static instance value.
   * @param value The value to assign.
   */
  static inline void set(T value) noexcept { instance_ = std::move(value); }

private:
  static inline T instance_ = {};
};
#elif (MICROFMT_TLS_MODEL == MICROFMT_TLS_MODEL_WIN32)
template <typename U>
constexpr inline bool can_fit_in_pointer_v = std::is_trivial_v<U> && (sizeof(U) <= sizeof(void *));

template <typename T, typename Tag, typename Enable = void> struct tls_provider;

struct win32_helpers {
  static DWORD create(PFLS_CALLBACK_FUNCTION deleter) noexcept {
    DWORD key = FlsAlloc(deleter);
    if (key == FLS_OUT_OF_INDEXES) {
      MICROFMT_TRAP();
    }
    return key;
  }

  static void *get(DWORD key) noexcept { return FlsGetValue(key); }

  static void set(DWORD key, void *p) noexcept {
    if (!FlsSetValue(key, p)) {
      MICROFMT_TRAP();
    }
  }

  template <typename T> static VOID NTAPI default_deleter(PVOID p) noexcept { delete static_cast<T *>(p); }
};

/// Specialization for Raw Pointers (T*)
template <typename T, typename Tag, typename Enable> struct tls_provider<T *, Tag, Enable> {
  [[nodiscard]] static inline T *get() noexcept {
    init_once();
    return static_cast<T *>(win32_helpers::get(key_));
  }

  /**
   * @brief Sets or updates the thread-local instance value.
   * @param value The value to assign.
   */
  static inline void set(T *value) noexcept {
    init_once();
    win32_helpers::set(key_, value);
  }

private:
  static BOOL CALLBACK init_callback(INIT_ONCE * /*InitOnce*/, PVOID /*Parameter*/, PVOID * /*Context*/) noexcept {
    key_ = win32_helpers::create(nullptr); // No destructor needed for raw pointers
    return TRUE;
  }

  static void init_once() noexcept { InitOnceExecuteOnce(&key_init_, init_callback, nullptr, nullptr); }

  static inline INIT_ONCE key_init_ = INIT_ONCE_STATIC_INIT;
  static inline DWORD key_ = FLS_OUT_OF_INDEXES;
};

/// Specialization for Heavy / Non-Trivial Values (Heap Allocated with Cleanup)
template <typename T, typename Tag>
struct tls_provider<T, Tag, std::enable_if_t<!std::is_pointer_v<T> && !can_fit_in_pointer_v<T>>> {
  /**
   * @brief Gets a reference to the thread-local instance, allocating it lazily if necessary.
   * @return Reference to the thread-local object.
   */
  [[nodiscard]] static inline T &get() noexcept {
    init_once();
    void *ptr = win32_helpers::get(key_);
    if (!ptr) {
      ptr = new T();
      MICROFMT_ASSERT(ptr != nullptr, "Object not created ?");
      win32_helpers::set(key_, ptr);
    }
    return *static_cast<T *>(ptr);
  }

  /**
   * @brief Sets or updates the thread-local instance value.
   * @param value The value to assign.
   */
  static inline void set(T value) noexcept {
    init_once();
    void *ptr = win32_helpers::get(key_);
    if (!ptr) {
      ptr = new T(std::move(value));
      MICROFMT_ASSERT(ptr != nullptr, "Object not created ?");
      win32_helpers::set(key_, ptr);
    } else {
      *static_cast<T *>(ptr) = std::move(value);
    }
  }

private:
  static BOOL CALLBACK init_callback(INIT_ONCE * /*InitOnce*/, PVOID /*Parameter*/, PVOID * /*Context*/) noexcept {
    key_ = win32_helpers::create(&win32_helpers::default_deleter<T>);
    return TRUE;
  }

  static void init_once() noexcept { InitOnceExecuteOnce(&key_init_, init_callback, nullptr, nullptr); }

  static inline INIT_ONCE key_init_ = INIT_ONCE_STATIC_INIT;
  static inline DWORD key_ = FLS_OUT_OF_INDEXES;
};

/// Specialization for Small Trivial Values (Zero Heap Allocation via Bit-Packing)
template <typename T, typename Tag>
struct tls_provider<T, Tag, std::enable_if_t<!std::is_pointer_v<T> && can_fit_in_pointer_v<T>>> {
  [[nodiscard]] static inline T get() noexcept {
    init_once();
    T value{};
    void *ptr = win32_helpers::get(key_);
    std::memcpy(&value, &ptr, sizeof(T));
    return value;
  }

  /**
   * @brief Sets or updates the thread-local instance value.
   * @param value The value to assign.
   */
  static inline void set(T value) noexcept {
    init_once();
    void *ptr = nullptr;
    std::memcpy(&ptr, &value, sizeof(T));
    win32_helpers::set(key_, ptr);
  }

private:
  static BOOL CALLBACK init_callback(INIT_ONCE * /*InitOnce*/, PVOID /*Parameter*/, PVOID * /*Context*/) noexcept {
    key_ = win32_helpers::create(nullptr); // No cleanup routine needed for inline bit-packed values
    return TRUE;
  }

  static void init_once() noexcept { InitOnceExecuteOnce(&key_init_, init_callback, nullptr, nullptr); }

  static inline INIT_ONCE key_init_ = INIT_ONCE_STATIC_INIT;
  static inline DWORD key_ = FLS_OUT_OF_INDEXES;
};
#else
#error "TLS model not set properly"
#endif

} // namespace microfmt::detail
