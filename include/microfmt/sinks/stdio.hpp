// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file stdio.hpp @brief FILE, standard-stream, and POSIX descriptor sinks. */

#include "../microfmt.hpp"
#include <cstdio>
#include <string_view>

#if defined(__unix__) || defined(__APPLE__) || defined(__posix)
#include <unistd.h>
#define MICROFMT_HAS_POSIX_FD 1
#else
#define MICROFMT_HAS_POSIX_FD 0
#endif

namespace microfmt {

// ============================================================================
// FILE* Sink Adapter
// ============================================================================

[[nodiscard]] inline sink file_sink(std::FILE *file) noexcept {
  return sink{file, [](void *ctx, microfmt::string_view sv) noexcept {
                if (ctx != nullptr && !sv.empty()) {
                  std::fwrite(sv.data(), 1, sv.size(),
                              static_cast<std::FILE *>(ctx));
                }
              }};
}

[[nodiscard]] inline sink stdout_sink() noexcept { return file_sink(stdout); }

[[nodiscard]] inline sink stderr_sink() noexcept { return file_sink(stderr); }

// ============================================================================
// POSIX File Descriptor Sink Adapter (write(2))
// ============================================================================

#if MICROFMT_HAS_POSIX_FD
[[nodiscard]] inline sink fd_sink(int fd) noexcept {
  // Store fd inside the pointer context value without allocation
  return sink{reinterpret_cast<void *>(static_cast<intptr_t>(fd)),
              [](void *ctx, microfmt::string_view sv) noexcept {
                const int target_fd =
                    static_cast<int>(reinterpret_cast<intptr_t>(ctx));
                if (target_fd >= 0 && !sv.empty()) {
                  const char *ptr = sv.data();
                  size_t remaining = sv.size();
                  while (remaining > 0) {
                    const ssize_t written = ::write(target_fd, ptr, remaining);
                    if (written <= 0) {
                      break; // Non-blocking full / broken pipe
                    }
                    ptr += written;
                    remaining -= static_cast<size_t>(written);
                  }
                }
              }};
}
#endif

// =============================================================================
// Direct stdout printing
// =============================================================================

// Compile-time stdout overloads
template <typename StrProvider, typename... Args>
inline void print(compile_string_holder<StrProvider> fmt,
                  const Args &...args) noexcept {
  auto s = stdout_sink();
  format_to(s, fmt, args...);
}

template <typename StrProvider, typename... Args>
inline void println(compile_string_holder<StrProvider> fmt,
                    const Args &...args) noexcept {
  auto s = stdout_sink();
  format_to(s, fmt, args...);
  s.put('\n');
}

// Runtime stdout overloads
template <typename... Args>
inline void print(microfmt::string_view fmt_str, const Args &...args) noexcept {
  auto s = stdout_sink();
  format_to(s, fmt_str, args...);
}

template <typename... Args>
inline void println(microfmt::string_view fmt_str, const Args &...args) noexcept {
  auto s = stdout_sink();
  format_to(s, fmt_str, args...);
  s.put('\n');
}

// =============================================================================
// Target std::FILE* stream printing
// =============================================================================

// Compile-time FILE* overloads
template <typename StrProvider, typename... Args>
inline void print(std::FILE *file, compile_string_holder<StrProvider> fmt,
                  const Args &...args) noexcept {
  auto s = file_sink(file);
  format_to(s, fmt, args...);
}

template <typename StrProvider, typename... Args>
inline void println(std::FILE *file, compile_string_holder<StrProvider> fmt,
                    const Args &...args) noexcept {
  auto s = file_sink(file);
  format_to(s, fmt, args...);
  s.put('\n');
}

// Runtime FILE* overloads
template <typename... Args>
inline void print(std::FILE *file, microfmt::string_view fmt_str,
                  const Args &...args) noexcept {
  auto s = file_sink(file);
  format_to(s, fmt_str, args...);
}

template <typename... Args>
inline void println(std::FILE *file, microfmt::string_view fmt_str,
                    const Args &...args) noexcept {
  auto s = file_sink(file);
  format_to(s, fmt_str, args...);
  s.put('\n');
}

// =============================================================================
// Target POSIX FD printing
// =============================================================================

#if MICROFMT_HAS_POSIX_FD
// Compile-time FD overloads
template <typename StrProvider, typename... Args>
inline void print(int fd, compile_string_holder<StrProvider> fmt,
                  const Args &...args) noexcept {
  auto s = fd_sink(fd);
  format_to(s, fmt, args...);
}

template <typename StrProvider, typename... Args>
inline void println(int fd, compile_string_holder<StrProvider> fmt,
                    const Args &...args) noexcept {
  auto s = fd_sink(fd);
  format_to(s, fmt, args...);
  s.put('\n');
}

// Runtime FD overloads
template <typename... Args>
inline void print(int fd, microfmt::string_view fmt_str,
                  const Args &...args) noexcept {
  auto s = fd_sink(fd);
  format_to(s, fmt_str, args...);
}

template <typename... Args>
inline void println(int fd, microfmt::string_view fmt_str,
                    const Args &...args) noexcept {
  auto s = fd_sink(fd);
  format_to(s, fmt_str, args...);
  s.put('\n');
}
#endif

// =============================================================================
// Target Sink printing
// =============================================================================

// Compile-time Sink overloads
template <typename StrProvider, typename... Args>
inline void print(sink s, compile_string_holder<StrProvider> fmt,
                  const Args &...args) noexcept {
  format_to(s, fmt, args...);
}

template <typename StrProvider, typename... Args>
inline void println(sink s, compile_string_holder<StrProvider> fmt,
                    const Args &...args) noexcept {
  format_to(s, fmt, args...);
  s.put('\n');
}

// Runtime Sink overloads
template <typename... Args>
inline void print(sink s, microfmt::string_view fmt_str,
                  const Args &...args) noexcept {
  format_to(s, fmt_str, args...);
}

template <typename... Args>
inline void println(sink s, microfmt::string_view fmt_str,
                    const Args &...args) noexcept {
  format_to(s, fmt_str, args...);
  s.put('\n');
}

// =============================================================================
// Bare newline helpers
// =============================================================================

inline void println(sink s) noexcept { s.put('\n'); }

inline void println() noexcept { stdout_sink().put('\n'); }

inline void println(std::FILE *file) noexcept { file_sink(file).put('\n'); }

#if MICROFMT_HAS_POSIX_FD
inline void println(int fd) noexcept { fd_sink(fd).put('\n'); }
#endif

} // namespace microfmt
