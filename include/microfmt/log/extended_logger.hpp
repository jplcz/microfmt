// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file extended_logger.hpp
 * @brief `basic_logger` derivative that stamps every record with execution
 * context metadata (pid/tid/CPU/flags) before handing it to advanced sinks.
 */

#include "extended_msg.hpp"
#include "extended_sink.hpp"
#include "logger.hpp"

namespace microfmt::log {

/**
 * @brief Metadata callback: describes the *current* execution context.
 *
 * Fills pid/tid/cpu and, most importantly, @ref log_flag::can_wait, which
 * tells sinks whether the caller may block when a sink is full. It runs on
 * every record on the emitting context, so it must be cheap and
 * context-safe (no allocation, no blocking).
 */
using log_meta_callback = log_meta (*)(void *user, const log_msg &msg) noexcept;

/** @brief Decorator turning a plain @ref log_msg into an @ref extended_log_msg. */
class RELOCO_POINTER meta_decorator {
public:
  void set_callback(log_meta_callback cb, void *user = nullptr) noexcept {
    cb_ = cb;
    user_ = user;
  }

  [[nodiscard]] extended_log_msg operator()(const log_msg &msg) const noexcept {
    return extended_log_msg(msg, cb_ != nullptr ? cb_(user_, msg) : log_meta{});
  }

  // Caller-built extended records already carry their metadata.
  [[nodiscard]] const extended_log_msg &operator()(const extended_log_msg &msg) const noexcept { return msg; }

private:
  log_meta_callback cb_{nullptr};
  void *user_{nullptr};
};

/**
 * @brief Logger for advanced sinks. Derives from @ref basic_logger, so the
 * whole formatting/level/locking API is inherited unchanged.
 *
 * @code
 * // Describe the current context; decides whether a full sink may be waited on.
 * microfmt::log::log_meta describe_context(void *, const microfmt::log::log_msg &) noexcept {
 *   microfmt::log::log_meta m;
 *   m.pid = current_pid();
 *   m.tid = current_tid();
 *   m.cpu = current_cpu();
 *   m.set(microfmt::log::log_flag::can_wait, !in_interrupt()); // IRQ: overwrite, never sleep
 *   return m;
 * }
 *
 * microfmt::log::extended_log_sink out = fan.as_sink(); // e.g. a fanout_log_sink
 * microfmt::log::extended_logger<> logger("kern", out);
 * logger.set_meta_callback(&describe_context);
 * logger.info("boot {}", 1);
 * @endcode
 */
template <std::size_t MaxSinks = 4, std::size_t MsgBufferCapacity = 256, typename Mutex = detail::null_mutex>
class extended_logger : public basic_logger<MaxSinks, MsgBufferCapacity, Mutex, extended_log_sink, meta_decorator> {
  using base = basic_logger<MaxSinks, MsgBufferCapacity, Mutex, extended_log_sink, meta_decorator>;

public:
  using base::base;
  using base::log;

  void set_meta_callback(log_meta_callback cb, void *user = nullptr) noexcept {
    this->decorator().set_callback(cb, user);
  }

  /** @brief Dispatches a caller-built record with explicit metadata. */
  void log(const extended_log_msg &msg) const noexcept {
    if (this->should_log(msg.lvl)) {
      this->dispatch(msg);
    }
  }
};

} // namespace microfmt::log
