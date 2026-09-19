// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file systemd_sink.hpp @brief Structured systemd journal sink adapter. */

#include "../log/sink.hpp"
#include <cstddef>
#include <string_view>
#include <sys/uio.h>
#include <syslog.h>
#include <systemd/sd-daemon.h>
#include <systemd/sd-journal.h>

namespace microfmt::log {

template <std::size_t MessageCapacity = 512,
          std::size_t IdentifierCapacity = 64>
/** @brief Adapter that writes structured records to the systemd journal. */
class systemd_sink {
  static_assert(MessageCapacity > 0,
                "Message capacity must be at least 1 byte");
  static_assert(IdentifierCapacity > 0,
                "Identifier capacity must be at least 1 byte");

public:
  using write_fn_t = void (*)(int priority, microfmt::string_view identifier,
                              microfmt::string_view message) noexcept;

  explicit constexpr systemd_sink(
      write_fn_t write_fn = write_to_journal) noexcept
      : write_fn_(write_fn) {}

  [[nodiscard]] log_sink as_sink() noexcept RELOCO_LIFETIMEBOUND {
    return log_sink{this,
                    [](void *ctx, const log_msg &msg) noexcept {
                      static_cast<systemd_sink *>(ctx)->log_impl(msg);
                    },
                    nullptr, level::trace};
  }

private:
  static void write_to_journal(int priority, microfmt::string_view identifier,
                               microfmt::string_view message) noexcept {
    if (::sd_booted() <= 0) {
      write_to_stdio(identifier, message);
      return;
    }

    char priority_field[] = "PRIORITY=0";
    priority_field[9] = static_cast<char>('0' + priority);

    buffer_sink<IdentifierCapacity + 18> identifier_field;
    const auto identifier_out = identifier_field.as_sink();
    identifier_out.write("SYSLOG_IDENTIFIER=");
    identifier_out.write(identifier.empty() ? "microfmt" : identifier);

    buffer_sink<MessageCapacity + 8> message_field;
    const auto message_out = message_field.as_sink();
    message_out.write("MESSAGE=");
    message_out.write(message);

    const auto identifier_view = identifier_field.view();
    const auto message_view = message_field.view();
    const iovec fields[] = {
        {priority_field, sizeof(priority_field) - 1},
        {const_cast<char *>(identifier_view.data()), identifier_view.size()},
        {const_cast<char *>(message_view.data()), message_view.size()},
    };
    if (::sd_journal_sendv(fields, 3) < 0) {
      write_to_stdio(identifier, message);
    }
  }

  static void write_to_stdio(microfmt::string_view identifier,
                             microfmt::string_view message) noexcept {
    buffer_sink<MessageCapacity + IdentifierCapacity + 4> output;
    const auto out = output.as_sink();
    if (!identifier.empty()) {
      format_to(out, MICROFMT_STRING("[{}] "), identifier);
    }
    format_to(out, MICROFMT_STRING("{}\n"), message);
    stdout_sink().write(output.view());
  }

  static int priority_for(level lvl) noexcept {
    switch (lvl) {
    case level::trace:
    case level::debug:
      return LOG_DEBUG;
    case level::info:
      return LOG_INFO;
    case level::warn:
      return LOG_WARNING;
    case level::err:
      return LOG_ERR;
    case level::critical:
      return LOG_CRIT;
    case level::off:
      return LOG_DEBUG;
    }
    return LOG_DEBUG;
  }

  void log_impl(const log_msg &msg) noexcept {
    if (msg.lvl != level::off) {
      write_fn_(priority_for(msg.lvl), msg.logger_name, msg.payload);
    }
  }

  write_fn_t write_fn_;
};

} // namespace microfmt::log
