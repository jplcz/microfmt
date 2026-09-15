// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file logger.hpp @brief Configurable structured loggers and helper functions. */

#include "sink.hpp"
#include "../array.hpp"
#include <cstddef>
#include <string_view>
#include <utility>

namespace microfmt::log {

/**
 * @brief Fixed-capacity logger that formats records before dispatching to sinks.
 *
 * @tparam MaxSinks Maximum number of attached structured log sinks.
 * @tparam MsgBufferCapacity Capacity of each formatted message payload.
 */
template <size_t MaxSinks = 4, size_t MsgBufferCapacity = 256>
class basic_logger {
public:
  explicit constexpr basic_logger(microfmt::string_view name) noexcept
      : name_(name) {}

  template <typename... Sinks>
    requires(sizeof...(Sinks) <= MaxSinks)
  explicit constexpr basic_logger(microfmt::string_view name,
                                  Sinks... sinks) noexcept
      : name_(name), sinks_{sinks...}, sink_count_(sizeof...(Sinks)) {}

  bool add_sink(log_sink s) noexcept {
    if (sink_count_ >= MaxSinks)
      return false;
    sinks_[sink_count_++] = s;
    return true;
  }

  void set_level(level l) noexcept { level_ = l; }
  [[nodiscard]] constexpr level get_level() const noexcept { return level_; }
  [[nodiscard]] constexpr microfmt::string_view name() const noexcept {
    return name_;
  }

  [[nodiscard]] bool should_log(level l) const noexcept { return l >= level_; }

  template <typename... Args>
  void log(level lvl, microfmt::string_view fmt_str, const Args &...args) noexcept {
    log_impl(std::source_location::current(), lvl, fmt_str, args...);
  }

  template <typename StrProvider, typename... Args>
  void log(level lvl, compile_string_holder<StrProvider> fmt_str,
           const Args &...args) noexcept {
    log_impl(std::source_location::current(), lvl, fmt_str, args...);
  }

  template <typename... Args>
  void trace(microfmt::string_view fmt, const Args &...args) noexcept {
    log(level::trace, fmt, args...);
  }

  template <typename StrProvider, typename... Args>
  void trace(compile_string_holder<StrProvider> fmt,
             const Args &...args) noexcept {
    log(level::trace, fmt, args...);
  }

  template <typename... Args>
  void debug(microfmt::string_view fmt, const Args &...args) noexcept {
    log(level::debug, fmt, args...);
  }

  template <typename StrProvider, typename... Args>
  void debug(compile_string_holder<StrProvider> fmt,
             const Args &...args) noexcept {
    log(level::debug, fmt, args...);
  }

  template <typename... Args>
  void info(microfmt::string_view fmt, const Args &...args) noexcept {
    log(level::info, fmt, args...);
  }

  template <typename StrProvider, typename... Args>
  void info(compile_string_holder<StrProvider> fmt,
            const Args &...args) noexcept {
    log(level::info, fmt, args...);
  }

  template <typename... Args>
  void warn(microfmt::string_view fmt, const Args &...args) noexcept {
    log(level::warn, fmt, args...);
  }

  template <typename StrProvider, typename... Args>
  void warn(compile_string_holder<StrProvider> fmt,
            const Args &...args) noexcept {
    log(level::warn, fmt, args...);
  }

  template <typename... Args>
  void error(microfmt::string_view fmt, const Args &...args) noexcept {
    log(level::err, fmt, args...);
  }

  template <typename StrProvider, typename... Args>
  void error(compile_string_holder<StrProvider> fmt,
             const Args &...args) noexcept {
    log(level::err, fmt, args...);
  }

  template <typename... Args>
  void critical(microfmt::string_view fmt, const Args &...args) noexcept {
    log(level::critical, fmt, args...);
  }

  template <typename StrProvider, typename... Args>
  void critical(compile_string_holder<StrProvider> fmt,
                const Args &...args) noexcept {
    log(level::critical, fmt, args...);
  }

  void flush() noexcept {
    for (size_t i = 0; i < sink_count_; ++i) {
      sinks_[i].flush();
    }
  }

  template <typename... Args>
  void log_loc(std::source_location loc, level lvl, microfmt::string_view fmt_str,
               const Args &...args) noexcept {
    log_impl(loc, lvl, fmt_str, args...);
  }

  template <typename StrProvider, typename... Args>
  void log_loc(std::source_location loc, level lvl,
               compile_string_holder<StrProvider> fmt_str,
               const Args &...args) noexcept {
    log_impl(loc, lvl, fmt_str, args...);
  }

private:
  template <typename Format, typename... Args>
  void log_impl(std::source_location loc, level lvl, Format fmt_str,
                const Args &...args) noexcept {
    if (!should_log(lvl) || sink_count_ == 0) {
      return;
    }

    // Format payload into internal line buffer
    buffer_sink<MsgBufferCapacity> buf;
    auto sink_stream = buf.as_sink();
    format_to(sink_stream, fmt_str, args...);

    log_msg msg{.logger_name = name_,
                .lvl = lvl,
                .time = std::chrono::system_clock::now(),
                .payload = buf.view(),
                .loc = loc};

    for (size_t i = 0; i < sink_count_; ++i) {
      sinks_[i].log(msg);
    }
  }

  microfmt::string_view name_{};
  level level_{level::info};
  microfmt::array<log_sink, MaxSinks> sinks_{};
  size_t sink_count_{0};
};

using logger = basic_logger<4, 256>;

namespace detail {

inline logger *&configured_default_logger() noexcept {
  static logger *instance = nullptr;
  return instance;
}

#if defined(MICROFMT_ENABLE_DEFAULT_LOGGER)
inline stdout_color_sink<256> &default_console_sink() noexcept {
  static stdout_color_sink<256> sink_instance;
  return sink_instance;
}

inline logger &built_in_default_logger() noexcept {
  static logger instance("app", default_console_sink().as_sink());
  return instance;
}
#endif

} // namespace detail

/** @brief Returns the configured default logger, or @c nullptr when unset. */
[[nodiscard]] inline logger *default_logger() noexcept {
  logger *configured = detail::configured_default_logger();
  if (configured != nullptr) {
    return configured;
  }

#if defined(MICROFMT_ENABLE_DEFAULT_LOGGER)
  return &detail::built_in_default_logger();
#else
  return nullptr;
#endif
}

/** @brief Sets or clears the process-default logger used by free helpers. */
inline void set_default_logger(logger *instance) noexcept {
  detail::configured_default_logger() = instance;
}

template <typename... Args>
inline void trace(microfmt::string_view fmt, const Args &...args) noexcept {
  if (logger *instance = default_logger()) {
    instance->trace(fmt, args...);
  }
}

template <typename StrProvider, typename... Args>
inline void trace(compile_string_holder<StrProvider> fmt,
                  const Args &...args) noexcept {
  if (logger *instance = default_logger()) {
    instance->trace(fmt, args...);
  }
}

template <typename... Args>
inline void debug(microfmt::string_view fmt, const Args &...args) noexcept {
  if (logger *instance = default_logger()) {
    instance->debug(fmt, args...);
  }
}

template <typename StrProvider, typename... Args>
inline void debug(compile_string_holder<StrProvider> fmt,
                  const Args &...args) noexcept {
  if (logger *instance = default_logger()) {
    instance->debug(fmt, args...);
  }
}

template <typename... Args>
inline void info(microfmt::string_view fmt, const Args &...args) noexcept {
  if (logger *instance = default_logger()) {
    instance->info(fmt, args...);
  }
}

template <typename StrProvider, typename... Args>
inline void info(compile_string_holder<StrProvider> fmt,
                 const Args &...args) noexcept {
  if (logger *instance = default_logger()) {
    instance->info(fmt, args...);
  }
}

template <typename... Args>
inline void warn(microfmt::string_view fmt, const Args &...args) noexcept {
  if (logger *instance = default_logger()) {
    instance->warn(fmt, args...);
  }
}

template <typename StrProvider, typename... Args>
inline void warn(compile_string_holder<StrProvider> fmt,
                 const Args &...args) noexcept {
  if (logger *instance = default_logger()) {
    instance->warn(fmt, args...);
  }
}

template <typename... Args>
inline void error(microfmt::string_view fmt, const Args &...args) noexcept {
  if (logger *instance = default_logger()) {
    instance->error(fmt, args...);
  }
}

template <typename StrProvider, typename... Args>
inline void error(compile_string_holder<StrProvider> fmt,
                  const Args &...args) noexcept {
  if (logger *instance = default_logger()) {
    instance->error(fmt, args...);
  }
}

template <typename... Args>
inline void critical(microfmt::string_view fmt, const Args &...args) noexcept {
  if (logger *instance = default_logger()) {
    instance->critical(fmt, args...);
  }
}

template <typename StrProvider, typename... Args>
inline void critical(compile_string_holder<StrProvider> fmt,
                     const Args &...args) noexcept {
  if (logger *instance = default_logger()) {
    instance->critical(fmt, args...);
  }
}

} // namespace microfmt::log