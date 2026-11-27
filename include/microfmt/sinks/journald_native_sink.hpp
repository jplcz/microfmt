// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file journald_native_sink.hpp
 * @brief Structured log sink speaking journald's native datagram protocol,
 * without linking libsystemd.
 *
 * Why: `systemd_sink` needs libsystemd. On minimal images or static builds
 * that library may be missing, but the journal itself only needs one
 * datagram sent to the Unix socket `/run/systemd/journal/socket`. The datagram
 * is plain text, `FIELD=value` lines; a value that contains a newline uses the
 * binary form `FIELD\n<8-byte little-endian length><value>\n`.
 *
 * How: create the socket once with `journald_native_open()` (or pass your own
 * datagram socket), build a `journald_native_sink`, and use `as_sink()` as a
 * log backend. Each record becomes one datagram carrying `PRIORITY`,
 * `SYSLOG_IDENTIFIER` (the logger name) and `MESSAGE`. Messages that do not
 * fit `Capacity` are truncated, never sent partially-formed. Failures are
 * counted in `dropped_records()`.
 */

#include "../log/sink.hpp"
#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>

#if !defined(_WIN32)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace microfmt::log {

template <std::size_t Capacity> class journald_native_sink;

/** @brief Tag selecting `journald_native_sink<Capacity>` as a `log_sink` backend. */
template <std::size_t Capacity> struct journald_native_sink_tag {};

#if !defined(_WIN32)
/** @brief Creates an unbound `AF_UNIX` datagram socket for the journal; returns -1 on failure. Caller closes it. */
[[nodiscard]] inline int journald_native_open() noexcept { return ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0); }
#endif

/** @brief Log adapter that sends each record to journald as one native-protocol datagram. */
template <std::size_t Capacity = 1024> class journald_native_sink {
  static_assert(Capacity >= 64, "Capacity must hold the field headers plus some message text");

public:
  /** @brief Sends one datagram on @p socket; returns true on success. */
  using send_fn_t = bool (*)(int socket, string_view datagram) noexcept;

#if !defined(_WIN32)
  /** @brief Borrows @p socket (see `journald_native_open()`); sends with `sendto(2)` to the journal socket. */
  explicit journald_native_sink(int socket) noexcept : socket_(socket), send_fn_(&send_to_journal) {}
#endif

  /** @brief Uses a custom send function (used by tests and other transports). */
  journald_native_sink(int socket, send_fn_t send_fn) noexcept : socket_(socket), send_fn_(send_fn) {}

  [[nodiscard]] log_sink as_sink() & noexcept RELOCO_LIFETIMEBOUND {
    return log_sink(journald_native_sink_tag<Capacity>{}, *this);
  }

  void log_impl(const log_msg &msg) noexcept {
    if (msg.lvl == level::off) {
      return;
    }
    buffer_sink<Capacity> datagram;
    const auto out = datagram.as_sink();
    microfmt::format_to(out, "PRIORITY={}\n", priority_for(msg.lvl));
    if (!msg.logger_name.empty()) {
      microfmt::format_to(out, "SYSLOG_IDENTIFIER={}\n", msg.logger_name);
    }
    const std::size_t room = Capacity - datagram.view().size();
    if (msg.payload.find('\n') == microfmt::string_view::npos) {
      constexpr std::size_t overhead = 9; // "MESSAGE=" and the final newline
      if (room <= overhead) {
        ++dropped_;
        return;
      }
      out.write("MESSAGE=");
      out.write(msg.payload.substr(0, room - overhead));
      out.write("\n");
    } else {
      constexpr std::size_t overhead = 8 + 8 + 1; // "MESSAGE\n", length field, final newline
      if (room <= overhead) {
        ++dropped_;
        return;
      }
      const microfmt::string_view body = msg.payload.substr(0, room - overhead);
      out.write("MESSAGE\n");
      std::uint64_t len = body.size();
      for (int i = 0; i < 8; ++i) {
        // NOLINTNEXTLINE(reloco-narrowing-flow): a length byte 0..255 is deliberately reinterpreted as a raw char
        out.put(static_cast<char>(len & 0xFFU));
        len >>= 8;
      }
      out.write(body);
      out.write("\n");
    }
    if (!send_fn_(socket_, datagram.view())) {
      ++dropped_;
    }
  }

  /** @brief Records that could not be sent (error, or no room for the message). */
  [[nodiscard]] std::size_t dropped_records() const noexcept { return dropped_; }

private:
#if !defined(_WIN32)
  // bridges to the C `sendto(2)` API, which takes raw pointers.
  RELOCO_BEGIN_UNSAFE_CODE
  static bool send_to_journal(int socket, string_view datagram) noexcept {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    constexpr string_view path = "/run/systemd/journal/socket";
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    for (std::size_t i = 0; i < path.size(); ++i) {
      addr.sun_path[i] = path[i];
    }
    RELOCO_END_UNSAFE_BUFFER_USAGE
    return ::sendto(socket, datagram.data(), datagram.size(), MSG_NOSIGNAL, reinterpret_cast<const sockaddr *>(&addr),
                    sizeof(addr)) >= 0;
  }
  RELOCO_END_UNSAFE_CODE
#endif

  static int priority_for(level lvl) noexcept {
    switch (lvl) {
    case level::trace:
    case level::debug:
      return 7;
    case level::info:
      return 6;
    case level::warn:
      return 4;
    case level::err:
      return 3;
    case level::critical:
      return 2;
    case level::off:
      return 7;
    }
    return 7;
  }

  int socket_{-1};
  send_fn_t send_fn_{nullptr};
  std::size_t dropped_{0};
};

template <std::size_t Capacity> struct log_sink_traits<journald_native_sink_tag<Capacity>> {
  using context_type = journald_native_sink<Capacity>;

  static void log(value_ref<context_type> ctx, const log_msg &msg) noexcept { ctx->log_impl(msg); }
};

} // namespace microfmt::log
