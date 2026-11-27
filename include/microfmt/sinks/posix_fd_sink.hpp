// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file posix_fd_sink.hpp
 * @brief Stateful sink and log adapter for a POSIX file descriptor with
 * short-write/`EINTR` handling and dropped-byte accounting.
 *
 * `microfmt::fd_sink(fd)` (in `stdio.hpp`) is the minimal stateless helper:
 * it gives up on the first short write or error and reports nothing. Use
 * `posix_fd_sink` instead when output must survive signals (`EINTR`), when the
 * descriptor is non-blocking (`EAGAIN`), or when you want to know how many
 * bytes were lost.
 */

#include "../log/detail/record_line.hpp"
#include "../log/sink.hpp"
#include "../microfmt.hpp"
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace microfmt {

/** @brief What `posix_fd_sink` does when the descriptor reports "try again" (`EAGAIN`). */
enum class posix_fd_policy : std::uint8_t {
  drop,  ///< Discard the unwritten bytes and count them in `dropped_bytes()` (never blocks).
  retry, ///< Spin until the descriptor accepts the bytes (may block the caller).
};

/**
 * @brief Writes formatted output to a borrowed file descriptor.
 *
 * The sink does not own or close the descriptor. Bytes that cannot be
 * delivered (hard error, or `EAGAIN` under `posix_fd_policy::drop`) are
 * dropped and counted instead of failing, so logging never throws or blocks
 * unexpectedly.
 */
class MICROFMT_API_CLASS posix_fd_sink {
public:
  /** @brief Signature of the platform write: returns bytes written, or <0 with `errno` set. */
  using write_fn_t = std::ptrdiff_t (*)(int fd, string_view data) noexcept;

#if !defined(_WIN32)
  /** @brief Constructs a sink over @p fd using POSIX `write(2)`. */
  explicit posix_fd_sink(int fd, posix_fd_policy policy = posix_fd_policy::drop) noexcept
      : fd_(fd), policy_(policy), write_fn_(&write_to_fd) {}
#endif

  /** @brief Constructs a sink over @p fd with a custom write function (used by tests and exotic targets). */
  posix_fd_sink(int fd, write_fn_t write_fn, posix_fd_policy policy = posix_fd_policy::drop) noexcept
      : fd_(fd), policy_(policy), write_fn_(write_fn) {}

  /** @brief Returns a type-erased `microfmt::sink` view over this instance. */
  [[nodiscard]] sink as_sink() & noexcept RELOCO_LIFETIMEBOUND {
    return sink{this, [](void *ctx, string_view sv) noexcept { static_cast<posix_fd_sink *>(ctx)->write(sv); }};
  }

  /** @brief Writes @p data, retrying short writes and `EINTR`. */
  void write(string_view data) noexcept {
    while (!data.empty()) {
      errno = 0;
      const std::ptrdiff_t written = write_fn_(fd_, data);
      if (written > 0) {
        // `written` is positive here; the mask only proves that to the narrowing analysis.
        data.remove_prefix(static_cast<std::size_t>(written & std::numeric_limits<std::ptrdiff_t>::max()));
        continue;
      }
      if (written < 0 && errno == EINTR) {
        continue;
      }
      if (written < 0 && policy_ == posix_fd_policy::retry && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        continue;
      }
      dropped_ += data.size();
      return;
    }
  }

  /** @brief Number of bytes that could not be delivered so far. */
  [[nodiscard]] std::size_t dropped_bytes() const noexcept { return dropped_; }

  /** @brief The borrowed descriptor. */
  [[nodiscard]] int fd() const noexcept { return fd_; }

private:
#if !defined(_WIN32)
  // bridges to the C `write(2)` API, which takes a raw pointer.
  RELOCO_BEGIN_UNSAFE_CODE
  static std::ptrdiff_t write_to_fd(int fd, string_view data) noexcept {
    return static_cast<std::ptrdiff_t>(::write(fd, data.data(), data.size()));
  }
  RELOCO_END_UNSAFE_CODE
#endif

  int fd_{-1};
  posix_fd_policy policy_{posix_fd_policy::drop};
  write_fn_t write_fn_{nullptr};
  std::size_t dropped_{0};
};

} // namespace microfmt

namespace microfmt::log {

template <std::size_t Capacity> class posix_fd_log_sink;

/** @brief Tag selecting `posix_fd_log_sink<Capacity>` as a `log_sink` backend. */
template <std::size_t Capacity> struct posix_fd_log_sink_tag {};

/** @brief Log adapter that writes each record as one `[logger] payload\n` line to a descriptor. */
template <std::size_t Capacity = 256> class posix_fd_log_sink {
  static_assert(Capacity > 1, "Capacity must leave room for the trailing newline");

public:
  explicit posix_fd_log_sink(const posix_fd_sink &target) noexcept : target_(target) {}

  [[nodiscard]] log_sink as_sink() & noexcept RELOCO_LIFETIMEBOUND {
    return log_sink(posix_fd_log_sink_tag<Capacity>{}, *this);
  }

  void log_impl(const log_msg &msg) noexcept {
    if (msg.lvl == level::off) {
      return;
    }
    buffer_sink<Capacity> buffer;
    detail::format_record_line(buffer, msg, true);
    target_.write(buffer.view());
  }

  /** @brief The underlying sink, e.g. to read `dropped_bytes()`. */
  [[nodiscard]] const posix_fd_sink &target() const & noexcept RELOCO_LIFETIMEBOUND { return target_; }
  const posix_fd_sink &target() const && = delete;

private:
  posix_fd_sink target_;
};

template <std::size_t Capacity> struct log_sink_traits<posix_fd_log_sink_tag<Capacity>> {
  using context_type = posix_fd_log_sink<Capacity>;

  static void log(value_ref<context_type> ctx, const log_msg &msg) noexcept { ctx->log_impl(msg); }
};

} // namespace microfmt::log
