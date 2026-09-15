// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdlib>

#if !defined(MICROFMT_DISABLE_ASSERT_STDIO)
#include <cstdio>
#endif

#if defined(__clang__) || defined(__GNUC__)
#define MICROFMT_TRAP() __builtin_trap()
#elif defined(_MSC_VER)
#define MICROFMT_TRAP() __debugbreak()
#else
#define MICROFMT_TRAP() std::abort()
#endif

#if __cplusplus >= 202002L
#define MICROFMT_ASSERT_UNLIKELY [[unlikely]]
#else
#define MICROFMT_ASSERT_UNLIKELY
#endif

namespace microfmt {

using assert_handler_t = void (*)(const char *expression, const char *file,
                                  int line, const char *message);

namespace detail {

inline void default_assert_handler(const char *expr, const char *file, int line,
                                   const char *msg) {
#if defined(MICROFMT_DISABLE_ASSERT_STDIO)
  (void)expr;
  (void)file;
  (void)line;
  (void)msg;
#else
  std::fprintf(stderr,
               "[MICROFMT ASSERT] Failure: %s\nAt: %s:%d\nMessage: %s\n", expr,
               file, line, msg);
#endif
}

inline assert_handler_t &get_handler_ptr() {
  static assert_handler_t handler = default_assert_handler;
  return handler;
}

} // namespace detail

inline void set_assert_handler(assert_handler_t new_handler) {
  detail::get_handler_ptr() = new_handler;
}

} // namespace microfmt

#if defined(MICROFMT_DISABLE_ASSERT)
#if defined(__clang__) || defined(__GNUC__)
#define MICROFMT_ASSERT(cond, ...)                                             \
  do {                                                                         \
    if (!(cond))                                                               \
      __builtin_unreachable();                                                 \
  } while (0)
#else
#define MICROFMT_ASSERT(cond, ...) (void)0
#endif
#else
#define MICROFMT_ASSERT(cond, ...)                                             \
  do {                                                                         \
    if (!(cond)) MICROFMT_ASSERT_UNLIKELY {                                    \
      ::microfmt::detail::get_handler_ptr()(#cond, __FILE__, __LINE__,         \
                                            "" __VA_ARGS__);                   \
      MICROFMT_TRAP();                                                         \
    }                                                                          \
  } while (0)
#endif

#if defined(NDEBUG) && !defined(MICROFMT_DEBUG)
#if defined(__clang__) || defined(__GNUC__)
#define MICROFMT_DEBUG_ASSERT(cond, ...)                                       \
  do {                                                                         \
    if (!(cond))                                                               \
      __builtin_unreachable();                                                 \
  } while (0)
#else
#define MICROFMT_DEBUG_ASSERT(cond, ...) (void)0
#endif
#else
#define MICROFMT_DEBUG_ASSERT(cond, ...)                                       \
  do {                                                                         \
    if (!(cond)) MICROFMT_ASSERT_UNLIKELY {                                    \
      ::microfmt::detail::get_handler_ptr()(#cond, __FILE__, __LINE__,         \
                                            "" __VA_ARGS__);                   \
      MICROFMT_TRAP();                                                         \
    }                                                                          \
  } while (0)
#endif
