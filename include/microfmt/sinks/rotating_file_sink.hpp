// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file rotating_file_sink.hpp
 * @brief File sink that rotates to numbered backups once a size limit is reached.
 *
 * Why: a log file that only grows eventually fills the disk. Rotation keeps
 * the newest `max_files` generations: `app.log` is the current file,
 * `app.log.1` the previous one, `app.log.2` the one before, and so on; the
 * oldest is overwritten.
 *
 * How: construct with a path, a size limit in bytes and the number of backups
 * to keep, then write through `as_sink()`. Before a write that would push the
 * current file past the limit, the sink closes it, renames the backups
 * (`.1` -> `.2`, ...) and opens a fresh file. A single write larger than the
 * limit is written whole into a fresh file, never split. Uses `posix_fd_sink`
 * for the actual writes, so it inherits its short-write and drop accounting.
 * Not thread-safe: wrap it in a lock if several threads write.
 */

#include "../microfmt.hpp"
#include "posix_fd_sink.hpp"
#include <cstddef>
#include <cstdint>

#if !defined(_WIN32)
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace microfmt {

/** @brief File-system operations used by `rotating_file_sink`; replace them to test or to target other OSes. */
struct rotating_file_ops {
  // NOLINTBEGIN(reloco-no-raw-pointer): C-string paths cross a C API boundary
  /** @brief Opens (creating/appending) @p path; returns a descriptor or -1. */
  using open_fn_t = int (*)(const char *path) noexcept;
  /** @brief Renames @p from to @p to, replacing @p to; returns true on success. */
  using rename_fn_t = bool (*)(const char *from, const char *to) noexcept;
  // NOLINTEND(reloco-no-raw-pointer)
  using close_fn_t = void (*)(int fd) noexcept;
  /** @brief Returns the current size of the open file in bytes. */
  using size_fn_t = std::size_t (*)(int fd) noexcept;

  open_fn_t open{nullptr};
  close_fn_t close{nullptr};
  rename_fn_t rename{nullptr};
  size_fn_t size{nullptr};
  posix_fd_sink::write_fn_t write{nullptr};
};

#if !defined(_WIN32)
namespace detail {
// bridges to the POSIX C APIs, which take raw pointers.
RELOCO_BEGIN_UNSAFE_CODE
inline int rotating_posix_open(const char *path) noexcept {
  return ::open(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
}
inline void rotating_posix_close(int fd) noexcept { ::close(fd); }
inline bool rotating_posix_rename(const char *from, const char *to) noexcept { return std::rename(from, to) == 0; }
inline std::size_t rotating_posix_size(int fd) noexcept {
  struct stat st{};
  return ::fstat(fd, &st) == 0 ? static_cast<std::size_t>(st.st_size) : 0;
}
inline std::ptrdiff_t rotating_posix_write(int fd, string_view data) noexcept {
  return static_cast<std::ptrdiff_t>(::write(fd, data.data(), data.size()));
}
RELOCO_END_UNSAFE_CODE
} // namespace detail

/** @brief The real POSIX file operations. */
[[nodiscard]] inline rotating_file_ops rotating_posix_ops() noexcept {
  return {&detail::rotating_posix_open, &detail::rotating_posix_close, &detail::rotating_posix_rename,
          &detail::rotating_posix_size, &detail::rotating_posix_write};
}
#endif

/** @brief Size-rotating file sink; @p PathCapacity bounds the path length (backup suffix included). */
template <std::size_t PathCapacity = 256> class rotating_file_sink {
  static_assert(PathCapacity > 16, "PathCapacity must hold a path plus a numeric suffix");

public:
  /**
   * @param path File to write; truncated if longer than `PathCapacity - 12`.
   * @param max_bytes Rotate before a write that would exceed this many bytes (non-zero).
   * @param max_backups Number of numbered backups to keep (at least 1).
   * @param ops File-system operations (defaults to POSIX where available).
   */
  rotating_file_sink(string_view path, std::size_t max_bytes, unsigned max_backups,
                     const rotating_file_ops &ops) noexcept
      : max_bytes_(max_bytes), max_backups_(max_backups == 0 ? 1U : max_backups), ops_(ops) {
    base_.as_sink().write(path.substr(0, PathCapacity - 12));
    open_current();
  }

#if !defined(_WIN32)
  rotating_file_sink(string_view path, std::size_t max_bytes, unsigned max_backups) noexcept
      : rotating_file_sink(path, max_bytes, max_backups, rotating_posix_ops()) {}
#endif

  rotating_file_sink(const rotating_file_sink &) = delete;
  rotating_file_sink &operator=(const rotating_file_sink &) = delete;
  ~rotating_file_sink() { close_current(); }

  /** @brief Returns a type-erased `microfmt::sink` view over this instance. */
  [[nodiscard]] sink as_sink() & noexcept RELOCO_LIFETIMEBOUND {
    return sink{this, [](void *ctx, string_view sv) noexcept { static_cast<rotating_file_sink *>(ctx)->write(sv); }};
  }

  /** @brief Appends @p data, rotating first when it would exceed the size limit. */
  void write(string_view data) noexcept {
    if (data.empty()) {
      return;
    }
    if (fd_ >= 0 && size_ > 0 && size_ + data.size() > max_bytes_) {
      rotate();
    }
    if (fd_ < 0) {
      dropped_ += data.size();
      return;
    }
    const std::size_t before = file_.dropped_bytes();
    file_.write(data);
    size_ += data.size() - (file_.dropped_bytes() - before);
  }

  /** @brief True while a file is open for writing. */
  [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

  /** @brief Bytes lost to write errors or to a file that could not be opened. */
  [[nodiscard]] std::size_t dropped_bytes() const noexcept { return dropped_ + file_.dropped_bytes(); }

  /** @brief Bytes in the current file. */
  [[nodiscard]] std::size_t current_size() const noexcept { return size_; }

private:
  // Fills @p out with "<base>" or "<base>.<n>" as a NUL-terminated string.
  void name_for(unsigned n, buffer_sink<PathCapacity> &out) const noexcept {
    const auto s = out.as_sink();
    s.write(base_.view());
    if (n > 0) {
      microfmt::format_to(s, ".{}", n);
    }
    s.write(string_view("\0", 1));
  }

  void open_current() noexcept {
    buffer_sink<PathCapacity> name;
    name_for(0, name);
    fd_ = ops_.open(name.view().data());
    size_ = fd_ >= 0 ? ops_.size(fd_) : 0;
    dropped_ += file_.dropped_bytes(); // keep the count across the replacement below
    file_ = posix_fd_sink(fd_, ops_.write);
  }

  void close_current() noexcept {
    if (fd_ >= 0) {
      ops_.close(fd_);
      fd_ = -1;
    }
  }

  void rotate() noexcept {
    close_current();
    for (unsigned n = max_backups_; n >= 1; --n) {
      buffer_sink<PathCapacity> from;
      buffer_sink<PathCapacity> to;
      name_for(n - 1, from);
      name_for(n, to);
      ops_.rename(from.view().data(), to.view().data()); // a missing generation is fine
    }
    open_current();
  }

  buffer_sink<PathCapacity> base_;
  std::size_t max_bytes_{0};
  unsigned max_backups_{1};
  rotating_file_ops ops_;
  int fd_{-1};
  std::size_t size_{0};
  std::size_t dropped_{0};
  posix_fd_sink file_{-1, nullptr};
};

} // namespace microfmt
