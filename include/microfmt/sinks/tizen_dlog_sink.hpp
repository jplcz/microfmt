#pragma once

#include "../log/sink.hpp"
#include <cstddef>
#include <string_view>

#include <dlog.h>

namespace microfmt::log {

template <std::size_t TagCapacity = 64> class tizen_dlog_sink {
  static_assert(TagCapacity > 0, "Tag capacity must be at least 1 byte");

public:
  using write_fn_t = void (*)(int priority, std::string_view tag,
                              std::string_view message) noexcept;

  explicit tizen_dlog_sink(std::string_view tag = "microfmt",
                           write_fn_t write_fn = write_to_dlog) noexcept
      : write_fn_(write_fn) {
    set_tag(tag);
  }

  [[nodiscard]] log_sink as_sink() noexcept {
    return log_sink{this,
                    [](void *ctx, const log_msg &msg) noexcept {
                      static_cast<tizen_dlog_sink *>(ctx)->log_impl(msg);
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
  static void write_to_dlog(int priority, std::string_view tag,
                            std::string_view message) noexcept {
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

  void log_impl(const log_msg &msg) noexcept {
    if (msg.lvl != level::off) {
      write_fn_(priority_for(msg.lvl), std::string_view(tag_, tag_size_),
                msg.payload);
    }
  }

  write_fn_t write_fn_;
  char tag_[TagCapacity]{};
  std::size_t tag_size_{0};
};

} // namespace microfmt::log
