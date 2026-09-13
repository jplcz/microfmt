#pragma once

/** @file android_log_sink.hpp @brief Structured Android logcat sink adapter. */

#include "../log/sink.hpp"
#include <cstddef>
#include <string_view>

#if defined(__ANDROID__)
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
inline int __android_log_write(int /*prio*/, const char * /*tag*/,
                               const char * /*text*/) {
  return 0;
}
#endif

namespace microfmt::log {

template <std::size_t MessageCapacity = 512, std::size_t TagCapacity = 64>
/** @brief Adapter that writes structured records to Android logcat. */
class android_log_sink {
  static_assert(MessageCapacity > 0, "Message capacity must be at least 1 byte");
  static_assert(TagCapacity > 0, "Tag capacity must be at least 1 byte");

public:
  using write_fn_t = void (*)(int priority, std::string_view tag,
                              std::string_view message) noexcept;

  explicit android_log_sink(std::string_view tag = "microfmt",
                            write_fn_t write_fn = write_to_logcat) noexcept
      : write_fn_(write_fn) {
    set_tag(tag);
  }

  [[nodiscard]] log_sink as_sink() noexcept {
    return log_sink{this,
                    [](void *ctx, const log_msg &msg) noexcept {
                      static_cast<android_log_sink *>(ctx)->log_impl(msg);
                    },
                    nullptr,
                    level::trace};
  }

  void set_tag(std::string_view tag) noexcept {
    tag_size_ = tag.size() < TagCapacity - 1 ? tag.size() : TagCapacity - 1;
    for (std::size_t i = 0; i < tag_size_; ++i) {
      tag_[i] = tag[i];
    }
    tag_[tag_size_] = '\0';
  }

private:
  static void write_to_logcat(int priority, std::string_view tag,
                              std::string_view message) noexcept {
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
    if (!msg.logger_name.empty()) {
      format_to(out, "[{}] ", msg.logger_name);
    }
    format_to(out, "{}", msg.payload);
    write_fn_(priority_for(msg.lvl),
              std::string_view(tag_, tag_size_),
              buffer.view());
  }

  write_fn_t write_fn_;
  char tag_[TagCapacity]{};
  std::size_t tag_size_{0};
};

} // namespace microfmt::log