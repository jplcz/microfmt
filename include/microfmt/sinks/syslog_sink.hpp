// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file syslog_sink.hpp @brief Structured POSIX syslog sink adapter. */

#include "../log/sink.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <syslog.h>

namespace microfmt::log {

/** @brief Adapter that writes structured records to POSIX syslog. */
template <std::size_t Capacity = 256> class syslog_sink {
  static_assert(Capacity > 0, "Capacity must be at least 1 byte");

public:
  using write_fn_t = void (*)(int priority,
                              microfmt::string_view message) noexcept;

  explicit constexpr syslog_sink(write_fn_t write_fn = write_to_syslog) noexcept
      : write_fn_(write_fn) {}

  [[nodiscard]] log_sink as_sink() noexcept {
    return log_sink{this,
                    [](void *ctx, const log_msg &msg) noexcept {
                      static_cast<syslog_sink *>(ctx)->log_impl(msg);
                    },
                    nullptr, level::trace};
  }

private:
  static void write_to_syslog(int priority,
                              microfmt::string_view message) noexcept {
    ::syslog(priority, "%.*s", static_cast<int>(message.size()),
             message.data());
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
    if (msg.lvl == level::off) {
      return;
    }

    buffer_sink<Capacity> buffer;
    const auto out = buffer.as_sink();
    if (!msg.logger_name.empty()) {
      format_to(out, MICROFMT_STRING("[{}] "), msg.logger_name);
    }
    format_to(out, MICROFMT_STRING("{}"), msg.payload);
    write_fn_(priority_for(msg.lvl), buffer.view());
  }

  write_fn_t write_fn_;
};

} // namespace microfmt::log