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

template <std::size_t Capacity> class syslog_sink;

/** @brief Tag selecting `syslog_sink<Capacity>` as a `log_sink` backend. */
template <std::size_t Capacity> struct syslog_sink_tag {};

/** @brief Adapter that writes structured records to POSIX syslog. */
template <std::size_t Capacity = 256> class syslog_sink {
  static_assert(Capacity > 0, "Capacity must be at least 1 byte");

public:
  using write_fn_t = void (*)(int priority, microfmt::string_view message) noexcept;

  explicit constexpr syslog_sink(write_fn_t write_fn = write_to_syslog) noexcept : write_fn_(write_fn) {}

  [[nodiscard]] log_sink as_sink() noexcept RELOCO_LIFETIMEBOUND { return log_sink(syslog_sink_tag<Capacity>{}, *this); }

  void log_impl(const log_msg &msg) noexcept {
    if (msg.lvl == level::off) {
      return;
    }

    buffer_sink<Capacity> buffer;
    const auto out = buffer.as_sink();
    if (!msg.logger_name.empty()) {
      microfmt::format_to(out, "[{}] ", msg.logger_name);
    }
    microfmt::format_to(out, "{}", msg.payload);
    write_fn_(priority_for(msg.lvl), buffer.view());
  }

private:
  static void write_to_syslog(int priority, microfmt::string_view message) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

    ::syslog(priority, "%.*s", static_cast<int>(message.size()), message.data());

    RELOCO_END_UNSAFE_BUFFER_USAGE;
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

  write_fn_t write_fn_;
};

template <std::size_t Capacity> struct log_sink_traits<syslog_sink_tag<Capacity>> {
  using context_type = syslog_sink<Capacity>;

  static void log(value_ref<context_type> ctx, const log_msg &msg) noexcept { ctx->log_impl(msg); }
};

} // namespace microfmt::log