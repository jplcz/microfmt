// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file tizen_dlog_sink.hpp @brief Structured Tizen DLOG sink adapter. */

#include "../log/detail/tagged_log_sink_base.hpp"
#include "../log/sink.hpp"
#include <cstddef>
#include <string_view>

#if MICROFMT_HAS_DLOG
#include <dlog.h>
#else
#include <cstdarg>
#include <cstdio>

// Fallback stubs for host-side unit testing / compilation outside the Tizen
// SDK. Rather than being a silent no-op, this prints to stdout so demos and
// manual testing still produce visible output on non-Tizen hosts.
enum log_priority {
  DLOG_UNKNOWN = 0,
  DLOG_DEFAULT,
  DLOG_VERBOSE,
  DLOG_DEBUG,
  DLOG_INFO,
  DLOG_WARN,
  DLOG_ERROR,
  DLOG_FATAL,
  DLOG_SILENT
};
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
inline int dlog_print(log_priority prio, const char *tag, const char *fmt, ...) {
  std::fprintf(stdout, "[%d] %s: ", static_cast<int>(prio), tag);
  va_list args;
  va_start(args, fmt);
  const int result = std::vfprintf(stdout, fmt, args);
  va_end(args);
  std::fprintf(stdout, "\n");
  return result;
}
RELOCO_END_UNSAFE_BUFFER_USAGE
#endif

namespace microfmt::log {

template <std::size_t TagCapacity> class tizen_dlog_sink;

/** @brief Tag selecting `tizen_dlog_sink<TagCapacity>` as a `log_sink` backend. */
template <std::size_t TagCapacity> struct tizen_dlog_sink_tag {};

/** @brief Adapter that writes structured records to Tizen DLOG. */
template <std::size_t TagCapacity = 64> class tizen_dlog_sink : public detail::tagged_log_sink_base<TagCapacity> {
  using tag_base = detail::tagged_log_sink_base<TagCapacity>;

public:
  using write_fn_t = void (*)(int priority, microfmt::string_view tag,
                              microfmt::string_view message) noexcept;

  explicit tizen_dlog_sink(microfmt::string_view tag = "microfmt",
                           write_fn_t write_fn = write_to_dlog) noexcept
      : tag_base(tag), write_fn_(write_fn) {}

  [[nodiscard]] log_sink as_sink() noexcept RELOCO_LIFETIMEBOUND {
    return log_sink(tizen_dlog_sink_tag<TagCapacity>{}, *this);
  }

  void log_impl(const log_msg &msg) noexcept {
    if (msg.lvl == level::off) {
      return;
    }

    char scratch[TagCapacity];
    const auto tag = this->resolve_tag(msg.logger_name, scratch);
    write_fn_(priority_for(msg.lvl), tag, msg.payload);
  }

private:
  static void write_to_dlog(int priority, microfmt::string_view tag,
                            microfmt::string_view message) noexcept {
    ::dlog_print(static_cast<::log_priority>(priority), tag.data(), "%.*s",
                 static_cast<int>(message.size()), message.data());
  }

  static int priority_for(level lvl) noexcept {
    switch (lvl) {
    case level::trace:
    case level::debug:
      return DLOG_DEBUG;
    case level::info:
      return DLOG_INFO;
    case level::warn:
      return DLOG_WARN;
    case level::err:
      return DLOG_ERROR;
    case level::critical:
      return DLOG_FATAL;
    case level::off:
      return DLOG_SILENT;
    }
    return DLOG_SILENT;
  }

  write_fn_t write_fn_;
};

template <std::size_t TagCapacity> struct log_sink_traits<tizen_dlog_sink_tag<TagCapacity>> {
  using context_type = tizen_dlog_sink<TagCapacity>;

  static void log(value_ref<context_type> ctx, const log_msg &msg) noexcept { ctx->log_impl(msg); }
};

} // namespace microfmt::log

