// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file socket_sink.hpp
 * @brief Sink and log adapter that send to a connected POSIX socket
 * (UDP = User Datagram Protocol, or TCP = Transmission Control Protocol).
 *
 * Use it to ship output to a remote collector (for example a `syslog` or
 * `netcat` listener). The caller creates and `connect(2)`s the socket; the
 * sink only sends on it.
 */

#include "../log/detail/record_line.hpp"
#include "../log/sink.hpp"
#include "../microfmt.hpp"
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>

#if !defined(_WIN32)
#include <sys/socket.h>
#include <sys/types.h>
#endif

namespace microfmt {

/** @brief How `socket_sink` maps `write()` calls onto the transport. */
enum class socket_sink_mode : std::uint8_t {
  stream,   ///< TCP-like: short sends are continued until all bytes are sent.
  datagram, ///< UDP-like: each `write()` is exactly one packet; it is sent once or dropped.
};

/**
 * @brief Sends formatted output on a borrowed, already connected socket.
 *
 * The sink does not own or close the socket. Data that cannot be sent is
 * dropped and counted in `dropped_bytes()`; it never blocks beyond what the
 * socket itself does and never raises `SIGPIPE` (uses `MSG_NOSIGNAL` where available).
 */
class MICROFMT_API_CLASS socket_sink {
public:
  /** @brief Signature of the platform send: returns bytes sent, or <0 with `errno` set. */
  using send_fn_t = std::ptrdiff_t (*)(int socket, string_view data) noexcept;

#if !defined(_WIN32)
  /** @brief Constructs a sink over the connected socket @p socket using POSIX `send(2)`. */
  explicit socket_sink(int socket, socket_sink_mode mode = socket_sink_mode::datagram) noexcept
      : socket_(socket), mode_(mode), send_fn_(&send_on_socket) {}
#endif

  /** @brief Constructs a sink with a custom send function (used by tests and other socket APIs). */
  socket_sink(int socket, send_fn_t send_fn, socket_sink_mode mode = socket_sink_mode::datagram) noexcept
      : socket_(socket), mode_(mode), send_fn_(send_fn) {}

  /** @brief Returns a type-erased `microfmt::sink` view over this instance. */
  [[nodiscard]] sink as_sink() & noexcept RELOCO_LIFETIMEBOUND {
    return sink{this, [](void *ctx, string_view sv) noexcept { static_cast<socket_sink *>(ctx)->write(sv); }};
  }

  /** @brief Sends @p data according to the configured mode; retries only on `EINTR`. */
  void write(string_view data) noexcept {
    if (data.empty()) {
      return;
    }
    while (!data.empty()) {
      errno = 0;
      const std::ptrdiff_t sent = send_fn_(socket_, data);
      if (sent < 0 && errno == EINTR) {
        continue;
      }
      if (sent <= 0) {
        break;
      }
      if (mode_ == socket_sink_mode::datagram) {
        return;
      }
      // `sent` is positive here; the mask only proves that to the narrowing analysis.
      data.remove_prefix(static_cast<std::size_t>(sent & std::numeric_limits<std::ptrdiff_t>::max()));
    }
    dropped_ += data.size();
  }

  /** @brief Number of bytes that could not be sent so far. */
  [[nodiscard]] std::size_t dropped_bytes() const noexcept { return dropped_; }

  /** @brief The borrowed socket descriptor. */
  [[nodiscard]] int socket() const noexcept { return socket_; }

private:
#if !defined(_WIN32)
  // bridges to the C `send(2)` API, which takes a raw pointer.
  RELOCO_BEGIN_UNSAFE_CODE
  static std::ptrdiff_t send_on_socket(int socket, string_view data) noexcept {
#if defined(MSG_NOSIGNAL)
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    return static_cast<std::ptrdiff_t>(::send(socket, data.data(), data.size(), flags));
  }
  RELOCO_END_UNSAFE_CODE
#endif

  int socket_{-1};
  socket_sink_mode mode_{socket_sink_mode::datagram};
  send_fn_t send_fn_{nullptr};
  std::size_t dropped_{0};
};

} // namespace microfmt

namespace microfmt::log {

template <std::size_t Capacity> class socket_log_sink;

/** @brief Tag selecting `socket_log_sink<Capacity>` as a `log_sink` backend. */
template <std::size_t Capacity> struct socket_log_sink_tag {};

/**
 * @brief Log adapter that sends each record as one `[logger] payload` message.
 *
 * No trailing newline is added in datagram mode (a packet already delimits the
 * record); stream mode appends `\n` so the receiver can split records.
 */
template <std::size_t Capacity = 256> class socket_log_sink {
  static_assert(Capacity > 1, "Capacity must leave room for the trailing newline");

public:
  explicit socket_log_sink(const socket_sink &target, bool newline = true) noexcept
      : target_(target), newline_(newline) {}

  [[nodiscard]] log_sink as_sink() & noexcept RELOCO_LIFETIMEBOUND {
    return log_sink(socket_log_sink_tag<Capacity>{}, *this);
  }

  void log_impl(const log_msg &msg) noexcept {
    if (msg.lvl == level::off) {
      return;
    }
    buffer_sink<Capacity> buffer;
    detail::format_record_line(buffer, msg, newline_);
    target_.write(buffer.view());
  }

  /** @brief The underlying sink, e.g. to read `dropped_bytes()`. */
  [[nodiscard]] const socket_sink &target() const & noexcept RELOCO_LIFETIMEBOUND { return target_; }
  const socket_sink &target() const && = delete;

private:
  socket_sink target_;
  bool newline_;
};

template <std::size_t Capacity> struct log_sink_traits<socket_log_sink_tag<Capacity>> {
  using context_type = socket_log_sink<Capacity>;

  static void log(value_ref<context_type> ctx, const log_msg &msg) noexcept { ctx->log_impl(msg); }
};

} // namespace microfmt::log
