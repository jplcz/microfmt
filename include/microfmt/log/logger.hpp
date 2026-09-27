// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file logger.hpp @brief Configurable structured loggers and helper functions. */

#include "sink.hpp"
#include "../reloco.hpp"
#include <cstddef>
#include <string_view>
#include <utility>

namespace microfmt::log {

namespace detail {

/**
 * @brief No-op mutex used as `basic_logger`'s default `Mutex` so
 * single-threaded usage pays zero synchronization cost.
 */
struct MICROFMT_API_CLASS null_mutex {
  constexpr void lock() noexcept {}
  constexpr void unlock() noexcept {}
  [[nodiscard]] constexpr bool try_lock() noexcept { return true; }
};

} // namespace detail

/**
 * @brief Fixed-capacity logger that formats records before dispatching to sinks.
 *
 * @tparam MaxSinks Maximum number of attached structured log sinks.
 * @tparam MsgBufferCapacity Capacity of each formatted message payload.
 * @tparam Mutex BasicLockable type (`lock()`/`unlock()`) serializing calls to
 * `log()`/`flush()`/`add_sink()` across threads. Defaults to a no-op lock, so
 * concurrent callers may interleave ("slice") sink writes; pass e.g.
 * `std::mutex` to serialize them instead when a single logger instance is
 * shared across threads doing normal (non-signal-handler) logging.
 *
 * @warning `basic_logger` is intended for normal program operation only, and
 * must never be reused from a signal/crash handler regardless of `Mutex`
 * choice: a blocking mutex can deadlock if the signal lands on a thread that
 * already holds it, and several sink backends (buffered stdio, syslog, the
 * systemd journal) aren't async-signal-safe to begin with. Crash handlers
 * must set up their own dedicated, minimal sink chain built directly on an
 * async-signal-safe primitive such as `fd_sink` (see `crash_handler_demo.cpp`)
 * instead of routing through an application's `basic_logger`.
 */
template <size_t MaxSinks = 4, size_t MsgBufferCapacity = 256,
          typename Mutex = detail::null_mutex>
class basic_logger {
  // Tiny RAII lock guard so this header doesn't need to pull in <mutex>
  // merely for std::lock_guard; works with any BasicLockable Mutex,
  // including std::mutex if the caller already includes <mutex>.
  struct lock_guard {
    explicit lock_guard(Mutex &m) noexcept : m_(m) { m_.lock(); }
    ~lock_guard() noexcept { m_.unlock(); }
    lock_guard(const lock_guard &) = delete;
    lock_guard &operator=(const lock_guard &) = delete;

    Mutex &m_;
  };

public:
  explicit constexpr basic_logger(microfmt::string_view name) noexcept
      : name_(name) {}

  template <typename... Sinks>
    requires(sizeof...(Sinks) <= MaxSinks)
  explicit constexpr basic_logger(microfmt::string_view name,
                                  Sinks... sinks) noexcept
      : name_(name), sinks_{sinks...}, sink_count_(sizeof...(Sinks)) {}

  bool add_sink(log_sink s) noexcept {
    lock_guard guard(mutex_);
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

  /**
   * @brief Dispatches a caller-built @ref log_msg directly to attached sinks.
   *
   * Bypasses this logger's own formatting step, so @p msg.payload is
   * forwarded to sinks verbatim. Still honors the logger's configured
   * level threshold. Useful for advanced usage such as forwarding records
   * from another logging system or replaying a previously formatted payload.
   */
  void log(const log_msg &msg) const noexcept {
    if (!should_log(msg.lvl) || sink_count_ == 0) {
      return;
    }
    lock_guard guard(mutex_);
    for (size_t i = 0; i < sink_count_; ++i) {
      sinks_[i].log(msg);
    }
  }

  template <typename... Args>
  void log(level lvl, microfmt::string_view fmt_str, const Args &...args) const noexcept {
    log_impl(std::source_location::current(), lvl, fmt_str, args...);
  }

  template <typename StrProvider, typename... Args>
  void log(level lvl, compile_string_holder<StrProvider> fmt_str,
           const Args &...args) const noexcept {
    log_impl(std::source_location::current(), lvl, fmt_str, args...);
  }

  template <typename... Args>
  void trace(microfmt::string_view fmt, const Args &...args) const noexcept {
    log(level::trace, fmt, args...);
  }

  template <typename StrProvider, typename... Args>
  void trace(compile_string_holder<StrProvider> fmt,
             const Args &...args) const noexcept {
    log(level::trace, fmt, args...);
  }

  template <typename... Args>
  void debug(microfmt::string_view fmt, const Args &...args) const noexcept {
    log(level::debug, fmt, args...);
  }

  template <typename StrProvider, typename... Args>
  void debug(compile_string_holder<StrProvider> fmt,
             const Args &...args) const noexcept {
    log(level::debug, fmt, args...);
  }

  template <typename... Args>
  void info(microfmt::string_view fmt, const Args &...args) const noexcept {
    log(level::info, fmt, args...);
  }

  template <typename StrProvider, typename... Args>
  void info(compile_string_holder<StrProvider> fmt,
            const Args &...args) const noexcept {
    log(level::info, fmt, args...);
  }

  template <typename... Args>
  void warn(microfmt::string_view fmt, const Args &...args) const noexcept {
    log(level::warn, fmt, args...);
  }

  template <typename StrProvider, typename... Args>
  void warn(compile_string_holder<StrProvider> fmt,
            const Args &...args) const noexcept {
    log(level::warn, fmt, args...);
  }

  template <typename... Args>
  void error(microfmt::string_view fmt, const Args &...args) const noexcept {
    log(level::err, fmt, args...);
  }

  template <typename StrProvider, typename... Args>
  void error(compile_string_holder<StrProvider> fmt,
             const Args &...args) const noexcept {
    log(level::err, fmt, args...);
  }

  template <typename... Args>
  void critical(microfmt::string_view fmt, const Args &...args) const noexcept {
    log(level::critical, fmt, args...);
  }

  template <typename StrProvider, typename... Args>
  void critical(compile_string_holder<StrProvider> fmt,
                const Args &...args) const noexcept {
    log(level::critical, fmt, args...);
  }

  void flush() const noexcept {
    lock_guard guard(mutex_);
    for (size_t i = 0; i < sink_count_; ++i) {
      sinks_[i].flush();
    }
  }

  template <typename... Args>
  void log_loc(std::source_location loc, level lvl, microfmt::string_view fmt_str,
               const Args &...args) const noexcept {
    log_impl(loc, lvl, fmt_str, args...);
  }

  template <typename StrProvider, typename... Args>
  void log_loc(std::source_location loc, level lvl,
               compile_string_holder<StrProvider> fmt_str,
               const Args &...args) const noexcept {
    log_impl(loc, lvl, fmt_str, args...);
  }

private:
  template <typename Format, typename... Args>
  void log_impl(std::source_location loc, level lvl, Format fmt_str,
                const Args &...args) const noexcept {
    if (!should_log(lvl) || sink_count_ == 0) {
      return;
    }

    // Format payload into internal line buffer
    buffer_sink<MsgBufferCapacity> buf;
    auto sink_stream = buf.as_sink();
    format_to(sink_stream, fmt_str, args...);

    log_msg msg{.logger_name = name_,
                .lvl = lvl,
                .time = microfmt::instant::now(),
                .payload = buf.view(),
                .loc = loc};

    lock_guard guard(mutex_);
    for (size_t i = 0; i < sink_count_; ++i) {
      sinks_[i].log(msg);
    }
  }

  microfmt::string_view name_{};
  level level_{level::info};
  mutable microfmt::array<log_sink, MaxSinks> sinks_{};
  size_t sink_count_{0};
  mutable Mutex mutex_{};
};

using logger = basic_logger<4, 256>;

namespace detail {

inline logger *&configured_default_logger() noexcept {
  static logger *instance = nullptr;
  return instance;
}

#if defined(MICROFMT_ENABLE_DEFAULT_LOGGER) && !defined(MICROFMT_DEFAULT_LOG_SINK_BACKEND_CUSTOM)
inline stdout_color_sink<256> &default_console_sink() noexcept {
  static stdout_color_sink<256> sink_instance;
  return sink_instance;
}

/** @brief Built-in backend: an ANSI-colorized `stdout_color_sink<256>`. */
inline logger &built_in_default_logger() noexcept {
  static logger instance("app", default_console_sink().as_sink());
  return instance;
}
#endif

} // namespace detail

} // namespace microfmt::log

#if defined(MICROFMT_ENABLE_DEFAULT_LOGGER) && defined(MICROFMT_DEFAULT_LOG_SINK_BACKEND_CUSTOM)
// MICROFMT_DEFAULT_LOG_SINK_BACKEND_CUSTOM opts out of the stdout backend
// above entirely, in favor of a platform-appropriate replacement (an OS's
// native logging facility, an RTOS/bare-metal console, ...) supplying the
// same `microfmt::log::detail::built_in_default_logger() noexcept ->
// logger &` entry point via a fixed include path, exactly like reloco's
// `detail/porting/mutex.hpp` does for `RELOCO_MUTEX_BACKEND_CUSTOM` (see
// `reloco/mutex.hpp`). Included unconditionally at this exact point --
// outside every namespace (the enclosing `namespace microfmt::log` is
// closed just above, and reopened just below), exactly like reloco's own
// fixed-path includes -- so nothing about where else this header is
// included from matters, and the included file is free to open/close
// `namespace microfmt::log`/`detail` itself, completely self-contained
// (never nested inside an already-open same-named namespace, which would
// otherwise silently declare a spurious `microfmt::log::microfmt::log`).
// Supplying the whole function (not merely a sink helper) gives a port
// full control over the resulting `logger` too -- its name, sink
// count/capacity, locking policy -- exactly as it would have if it wrote
// `built_in_default_logger()` itself. This file does not ship in this
// repository (only `detail/porting/default_log_sink.template.hpp`, an
// unused documentation-only scaffold, does), so you supply it yourself,
// most conveniently through the `JPLCZ_MICROFMT_PORTING_HEADERS` CMake
// variable (see `CMakeLists.txt`), which copies it into that exact path
// and bakes `MICROFMT_DEFAULT_LOG_SINK_BACKEND_CUSTOM` into a generated
// `detail/porting/generated_config.hpp` that `microfmt_config.hpp` picks
// up automatically for every consumer of the plain `include/` tree, not
// merely a consumer linking the `jplcz_microfmt` CMake target -- or by
// placing it there manually and defining the macro yourself if not using
// CMake. See `detail/porting/README.md` and `docs/porting.md`.
#include "../detail/porting/default_log_sink.hpp"
#endif

namespace microfmt::log {

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