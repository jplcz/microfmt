#pragma once

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
  return sink{file, [](void *ctx, std::string_view sv) noexcept {
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
              [](void *ctx, std::string_view sv) noexcept {
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

// ============================================================================
// Print / Println Helpers
// ============================================================================

// Direct stdout printing
template <typename... Args>
void print(std::string_view fmt_str, const Args &...args) noexcept {
  auto s = stdout_sink();
  format_to(s, fmt_str, args...);
}

template <typename... Args>
void println(std::string_view fmt_str, const Args &...args) noexcept {
  auto s = stdout_sink();
  format_to(s, fmt_str, args...);
  s.put('\n');
}

// Target FILE* stream printing
template <typename... Args>
void print(std::FILE *file, std::string_view fmt_str,
           const Args &...args) noexcept {
  auto s = file_sink(file);
  format_to(s, fmt_str, args...);
}

template <typename... Args>
void println(std::FILE *file, std::string_view fmt_str,
             const Args &...args) noexcept {
  auto s = file_sink(file);
  format_to(s, fmt_str, args...);
  s.put('\n');
}

#if MICROFMT_HAS_POSIX_FD
// Target FD printing
template <typename... Args>
void print(int fd, std::string_view fmt_str, const Args &...args) noexcept {
  auto s = fd_sink(fd);
  format_to(s, fmt_str, args...);
}

template <typename... Args>
void println(int fd, std::string_view fmt_str, const Args &...args) noexcept {
  auto s = fd_sink(fd);
  format_to(s, fmt_str, args...);
  s.put('\n');
}
#endif

} // namespace microfmt
