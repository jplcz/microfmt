// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file android_log_sink.hpp @brief Structured Android logcat sink adapter. */

#include "../log/sink.hpp"
#include <cstddef>
#include <string_view>

#if MICROFMT_HAS_ANDROID_LOG
#include <android/log.h>
#else
// Fallback stubs for host-side unit testing / compilation outside NDK
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
inline int __android_log_write(int /*prio*/, const char * /*tag*/, const char * /*text*/) { return 0; }
#endif

namespace microfmt::log {

template <std::size_t MessageCapacity = 512, std::size_t TagCapacity = 64>
/** @brief Adapter that writes structured records to Android logcat. */
class android_log_sink {
  static_assert(MessageCapacity > 0, "Message capacity must be at least 1 byte");
  static_assert(TagCapacity > 0, "Tag capacity must be at least 1 byte");

public:
  using write_fn_t = void (*)(int priority, microfmt::string_view tag, microfmt::string_view message) noexcept;

  explicit android_log_sink(microfmt::string_view tag = "microfmt", write_fn_t write_fn = write_to_logcat) noexcept
      : write_fn_(write_fn) {
    set_tag(tag);
  }

  [[nodiscard]] log_sink as_sink() noexcept MICROFMT_LIFETIMEBOUND {
    return log_sink{this,
                    [](void *ctx, const log_msg &msg) noexcept { static_cast<android_log_sink *>(ctx)->log_impl(msg); },
                    nullptr, level::trace};
  }

  void set_tag(microfmt::string_view tag) noexcept {
    MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

    tag_size_ = tag.size() < TagCapacity - 1 ? tag.size() : TagCapacity - 1;
    for (std::size_t i = 0; i < tag_size_; ++i) {
      tag_[i] = tag[i];
    }
    tag_[tag_size_] = '\0';

    MICROFMT_END_UNSAFE_BUFFER_USAGE;
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

  void log_impl(const log_msg &msg) noexcept {
    if (msg.lvl == level::off) {
      return;
    }

    buffer_sink<MessageCapacity> buffer;
    const auto out = buffer.as_sink();
    format_to(out, MICROFMT_STRING("{}"), msg.payload);

    // Prefer the originating logger's name as the logcat tag so records from
    // different loggers stay distinguishable in filters; fall back to the
    // sink's configured default tag when the message carries none.
    if (msg.logger_name.empty()) {
      write_fn_(priority_for(msg.lvl), microfmt::string_view(tag_, tag_size_), buffer.view());
      return;
    }

    MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

    char logger_tag[TagCapacity];
    const std::size_t logger_tag_size =
        msg.logger_name.size() < TagCapacity - 1 ? msg.logger_name.size() : TagCapacity - 1;
    for (std::size_t i = 0; i < logger_tag_size; ++i) {
      logger_tag[i] = msg.logger_name[i];
    }
    logger_tag[logger_tag_size] = '\0';

    MICROFMT_END_UNSAFE_BUFFER_USAGE;

    write_fn_(priority_for(msg.lvl), microfmt::string_view(logger_tag, logger_tag_size), buffer.view());
  }

  write_fn_t write_fn_;
  char tag_[TagCapacity]{};
  std::size_t tag_size_{0};
};

} // namespace microfmt::log