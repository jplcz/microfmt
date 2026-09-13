#pragma once

#include "microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <syslog.h>

namespace microfmt {

// ============================================================================
// Syslog Severity Priorities
// ============================================================================

enum class log_priority : int {
  emerg = LOG_EMERG,
  alert = LOG_ALERT,
  crit = LOG_CRIT,
  err = LOG_ERR,
  warning = LOG_WARNING,
  notice = LOG_NOTICE,
  info = LOG_INFO,
  debug = LOG_DEBUG
};

// ============================================================================
// Zero-Allocation Line-Buffered Syslog Sink
// ============================================================================

template <size_t Capacity = 256> class syslog_sink {
  static_assert(Capacity > 0, "Capacity must be at least 1 byte");

public:
  explicit constexpr syslog_sink(
      log_priority prio = log_priority::info) noexcept
      : priority_(static_cast<int>(prio)) {}

  ~syslog_sink() noexcept { flush(); }

  // Non-copyable, movable
  syslog_sink(const syslog_sink &) = delete;
  syslog_sink &operator=(const syslog_sink &) = delete;
  syslog_sink(syslog_sink &&other) noexcept { move_from(std::move(other)); }
  syslog_sink &operator=(syslog_sink &&other) noexcept {
    if (this != &other) {
      flush();
      move_from(std::move(other));
    }
    return *this;
  }

  [[nodiscard]] sink as_sink() noexcept {
    return sink{this, [](void *ctx, std::string_view sv) noexcept {
                  static_cast<syslog_sink *>(ctx)->write(sv);
                }};
  }

  void set_priority(log_priority prio) noexcept {
    if (size_ > 0 && priority_ != static_cast<int>(prio)) {
      flush();
    }
    priority_ = static_cast<int>(prio);
  }

  void put(char c) noexcept {
    if (c == '\n') {
      flush();
      return;
    }

    if (size_ + 1 >= Capacity) { // Reserve 1 byte for null terminator
      flush();
    }

    buffer_[size_++] = c;
  }

  void write(std::string_view sv) noexcept {
    for (char c : sv) {
      put(c);
    }
  }

  void flush() noexcept {
    if (size_ == 0) {
      return;
    }

    buffer_[size_] = '\0';
    ::syslog(priority_, "%s", buffer_);
    size_ = 0;
  }

private:
  void move_from(syslog_sink &&other) noexcept {
    priority_ = other.priority_;
    size_ = other.size_;
    for (size_t i = 0; i < size_; ++i) {
      buffer_[i] = other.buffer_[i];
    }
    other.size_ = 0;
  }

  int priority_{LOG_INFO};
  size_t size_{0};
  char buffer_[Capacity]{};
};

// ============================================================================
// Quick-Logging Helpers
// ============================================================================

template <size_t Capacity = 256, typename... Args>
inline void syslog(log_priority prio, std::string_view fmt_str,
                   const Args &...args) {
  syslog_sink<Capacity> log(prio);
  auto s = log.as_sink();
  format_to(s, fmt_str, args...);
  // Flushes on scope exit
}

} // namespace microfmt