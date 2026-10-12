// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file sync.hpp
 * @brief Locking and wait-channel building blocks shared by the logging
 * stack (@ref basic_logger and the extended sink fan-out).
 *
 * A **wait channel** couples the "outer" lock a blocked writer sleeps on with
 * the primitive that sleeps/wakes it, so a port can map both onto one native
 * facility (a condition variable + mutex on hosted targets, `mtx(9)` +
 * `cv(9)`/`wakeup(9)` in a FreeBSD kernel). The required shape:
 *
 * @code
 * struct my_wait_channel {
 *   void lock() noexcept;     // outer lock; held while checking the predicate
 *   void unlock() noexcept;
 *
 *   // Called with the lock held. Sleeps (releasing the lock while asleep)
 *   // until pred() is true or timeout_us microseconds passed
 *   // (`wait_forever`: no deadline). Re-evaluates
 *   // pred() with the lock held. Returns the final pred() value.
 *   template <typename Pred> bool wait_for(std::uint64_t timeout_us, Pred pred) noexcept;
 *
 *   void wake_all() noexcept; // wakes every sleeper; safe with the lock held
 * };
 *
 * // FreeBSD kernel sketch (not compiled here):
 * //   lock()     -> mtx_lock(&mtx_)
 * //   wait_for() -> while (!pred()) { if (cv_timedwait(&cv_, &mtx_, tick_count(us)) == EWOULDBLOCK) break; }
 * //   wake_all() -> cv_broadcast(&cv_)
 * @endcode
 */

#include "../microfmt_config.hpp"
#include <cstdint>
#include <reloco/mutex.hpp>
#include <reloco/unique_lock.hpp>

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

/** @brief `timeout_us` value meaning "wait without a deadline". */
inline constexpr std::uint64_t wait_forever = ~std::uint64_t{0};

/**
 * @brief Wait channel that never sleeps: `wait_for` just evaluates the
 * predicate once. Use for single-threaded or "discard when full" setups.
 */
struct null_wait_channel {
  constexpr void lock() noexcept {}
  constexpr void unlock() noexcept {}
  template <typename Pred> [[nodiscard]] constexpr bool wait_for(std::uint64_t, Pred pred) noexcept {
    return static_cast<bool>(pred());
  }
  constexpr void wake_all() noexcept {}
};

/**
 * @brief Hosted wait channel: a `reloco::mutex` plus `reloco::condition_variable`.
 *
 * @code
 * // Outer lock + sleep/wake primitive for a fan-out whose writers may block.
 * microfmt::log::fanout_log_sink<4, microfmt::log::condition_variable_wait_channel> fan;
 * @endcode
 */
class condition_variable_wait_channel {
public:
  void lock() noexcept { mutex_.lock(); }
  void unlock() noexcept { mutex_.unlock(); }

  template <typename Pred> [[nodiscard]] bool wait_for(std::uint64_t timeout_us, Pred pred) noexcept {
    // The caller already owns mutex_; adopt it for the wait and hand it back.
    reloco::unique_lock<reloco::mutex> guard(mutex_, reloco::adopt_lock);
    bool ok = true;
    if (timeout_us == wait_forever) {
      ok = cv_.wait(guard, pred).has_value();
    } else {
      auto res = cv_.wait_for(guard, reloco::duration::from_micros(timeout_us), pred);
      ok = res.has_value() && *res;
    }
    (void)guard.release();
    return ok;
  }

  void wake_all() noexcept { cv_.notify_all(); }

private:
  reloco::mutex mutex_{};
  reloco::condition_variable cv_{};
};

/**
 * @brief Type-erased "space was freed" callback handed to the consumer side
 * of a sink (two words, no allocation).
 *
 * The consumer calls @ref notify after draining data so blocked writers wake.
 */
class RELOCO_POINTER space_notifier {
public:
  using callback = void (*)(void *) noexcept;

  constexpr space_notifier() noexcept = default;
  constexpr space_notifier(void *ctx, callback cb) noexcept : ctx_(ctx), cb_(cb) {}

  void notify() const noexcept {
    if (cb_ != nullptr) {
      cb_(ctx_);
    }
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return cb_ != nullptr; }

private:
  void *ctx_{nullptr};
  callback cb_{nullptr};
};

} // namespace microfmt::log
