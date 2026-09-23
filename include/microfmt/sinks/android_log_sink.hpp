// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file android_log_sink.hpp @brief Structured Android logcat sink adapter. */

#include "../log/detail/tagged_log_sink_base.hpp"
#include "../log/sink.hpp"
#include <cstddef>
#include <string_view>

#if MICROFMT_HAS_ANDROID_LOG
#include <android/log.h>
#else
#include <cstdio>

// Fallback stubs for host-side unit testing / compilation outside the NDK.
// Rather than being a silent no-op, this prints to stdout so demos and
// manual testing still produce visible output on non-Android hosts.
enum android_LogPriority {
  ANDROID_LOG_UNKNOWN = 0,
  ANDROID_LOG_DEFAULT,
  ANDROID_LOG_VERBOSE,
  ANDROID_LOG_DEBUG,
  ANDROID_LOG_INFO,
  ANDROID_LOG_WARN,
  ANDROID_LOG_ERROR,
  ANDROID_LOG_FATAL,
  ANDROID_LOG_SILENT
};
inline int __android_log_write(int prio, const char *tag, const char *text) {
  std::fprintf(stdout, "[%d] %s: %s\n", prio, tag, text);
  return 0;
}
#endif

namespace microfmt::log {

template <std::size_t MessageCapacity, std::size_t TagCapacity> class android_log_sink;

/** @brief Tag selecting `android_log_sink<MessageCapacity, TagCapacity>` as a `log_sink` backend. */
template <std::size_t MessageCapacity, std::size_t TagCapacity> struct android_log_sink_tag {};

template <std::size_t MessageCapacity = 512, std::size_t TagCapacity = 64>
/** @brief Adapter that writes structured records to Android logcat. */
class android_log_sink : public detail::tagged_log_sink_base<TagCapacity> {
  static_assert(MessageCapacity > 0, "Message capacity must be at least 1 byte");

  using tag_base = detail::tagged_log_sink_base<TagCapacity>;

public:
  using write_fn_t = void (*)(int priority, microfmt::string_view tag, microfmt::string_view message) noexcept;

  explicit android_log_sink(microfmt::string_view tag = "microfmt", write_fn_t write_fn = write_to_logcat) noexcept
      : tag_base(tag), write_fn_(write_fn) {}

  [[nodiscard]] log_sink as_sink() noexcept RELOCO_LIFETIMEBOUND {
    return log_sink(android_log_sink_tag<MessageCapacity, TagCapacity>{}, *this);
  }

  void log_impl(const log_msg &msg) noexcept {
    if (msg.lvl == level::off) {
      return;
    }

    buffer_sink<MessageCapacity> buffer;
    const auto out = buffer.as_sink();
    microfmt::format_to(out, "{}", msg.payload);

    char scratch[TagCapacity];
    const auto tag = this->resolve_tag(msg.logger_name, scratch);
    write_fn_(priority_for(msg.lvl), tag, buffer.view());
  }

private:
  static void write_to_logcat(int priority, microfmt::string_view tag, microfmt::string_view message) noexcept {
    ::__android_log_write(priority, tag.data(), message.data());
  }

  static int priority_for(level lvl) noexcept {
    switch (lvl) {
    case level::trace:
      return ANDROID_LOG_VERBOSE;
    case level::debug:
      return ANDROID_LOG_DEBUG;
    case level::info:
      return ANDROID_LOG_INFO;
    case level::warn:
      return ANDROID_LOG_WARN;
    case level::err:
      return ANDROID_LOG_ERROR;
    case level::critical:
      return ANDROID_LOG_FATAL;
    case level::off:
      return ANDROID_LOG_SILENT;
    }
    return ANDROID_LOG_SILENT;
  }

  write_fn_t write_fn_;
};

template <std::size_t MessageCapacity, std::size_t TagCapacity>
struct log_sink_traits<android_log_sink_tag<MessageCapacity, TagCapacity>> {
  using context_type = android_log_sink<MessageCapacity, TagCapacity>;

  static void log(value_ref<context_type> ctx, const log_msg &msg) noexcept { ctx->log_impl(msg); }
};

} // namespace microfmt::log
