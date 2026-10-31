// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file extended_sink.hpp
 * @brief "Advanced" log sinks that see @ref extended_log_msg metadata and can
 * report being full, plus a fixed-capacity fan-out sink that applies
 * back-pressure across several of them.
 *
 * These sinks intentionally do *not* mirror the legacy @ref log_sink API:
 * their `log` takes an @ref extended_log_msg and they may expose `full`.
 *
 * @code
 * // A backend: tag + traits. `context_type` holds the state.
 * struct my_ring_tag {};
 * struct my_ring { / * ring buffer, its own lock, ... * / };
 *
 * template <> struct microfmt::log::extended_log_sink_traits<my_ring_tag> {
 *   using context_type = my_ring;
 *
 *   // Mandatory. msg.meta.can_wait() == false (IRQ): overwrite the oldest
 *   // data when full. == true (thread): the fan-out already waited for
 *   // space, so just append, and drop only if that raced.
 *   static void log(microfmt::value_ref<my_ring> ctx, const microfmt::log::extended_log_msg &msg) noexcept;
 *
 *   // Optional. true when `msg` would not fit without waiting/overwriting.
 *   static bool full(microfmt::value_ref<my_ring> ctx, const microfmt::log::extended_log_msg &msg) noexcept;
 *
 *   // Optional.
 *   static void flush(microfmt::value_ref<my_ring> ctx) noexcept;
 * };
 * @endcode
 */

#include "../microfmt.hpp"
#include "extended_msg.hpp"
#include "sink.hpp"
#include "sync.hpp"
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace microfmt::log {

/**
 * @brief Customization point for @ref extended_log_sink backends.
 *
 * Requires a non-void `context_type` and `log(value_ref<context_type>, const
 * extended_log_msg &) noexcept`; `full(value_ref<context_type>, const
 * extended_log_msg &) -> bool` and `flush(value_ref<context_type>)` are optional.
 */
template <typename Tag> struct extended_log_sink_traits;

namespace detail {

template <typename Tag>
concept has_extended_full =
    requires(value_ref<typename extended_log_sink_traits<Tag>::context_type> ctx, const extended_log_msg &msg) {
      { extended_log_sink_traits<Tag>::full(ctx, msg) } -> std::convertible_to<bool>;
    };

template <typename Tag>
concept has_extended_flush = requires(value_ref<typename extended_log_sink_traits<Tag>::context_type> ctx) {
  extended_log_sink_traits<Tag>::flush(ctx);
};

} // namespace detail

/**
 * @brief Type-erased handle to an advanced log sink backend, with a severity
 * threshold. Same two-word-plus-vtable shape as @ref log_sink.
 */
class MICROFMT_API_CLASS RELOCO_POINTER extended_log_sink {
public:
  struct vtable {
    void (*log)(void *ctx, const extended_log_msg &msg) noexcept = nullptr;
    /** @brief `nullptr` when the backend cannot report fullness. */
    bool (*full)(void *ctx, const extended_log_msg &msg) noexcept = nullptr;
    /** @brief `nullptr` when the backend cannot flush. */
    void (*flush)(void *ctx) noexcept = nullptr;
  };

  constexpr extended_log_sink() noexcept = default;

  template <typename Tag, typename Context, typename Traits = extended_log_sink_traits<Tag>>
    requires(std::is_convertible_v<Context *, typename Traits::context_type *>)
  constexpr extended_log_sink(Tag, Context &ctx RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS,
                              level lvl = level::trace) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag>), lvl_(lvl) {}

  // A temporary context would dangle immediately.
  template <typename Tag, typename Context>
    requires(!std::is_lvalue_reference_v<Context>)
  constexpr extended_log_sink(Tag, Context &&, level = level::trace) = delete;

  /** @brief Emits @p msg unless it is below this sink's threshold. */
  void log(const extended_log_msg &msg) noexcept {
    if (vtbl_ != nullptr && msg.lvl >= lvl_) {
      vtbl_->log(ctx_.get(), msg);
    }
  }

  /** @brief Reports whether the backend can say it is full. */
  [[nodiscard]] constexpr bool can_report_full() const noexcept { return vtbl_ != nullptr && vtbl_->full != nullptr; }

  /**
   * @brief Whether @p msg could not be accepted right now without waiting or
   * overwriting. `false` for backends that cannot report fullness and for
   * records the threshold would drop anyway.
   */
  [[nodiscard]] bool full(const extended_log_msg &msg) noexcept {
    return can_report_full() && msg.lvl >= lvl_ && vtbl_->full(ctx_.get(), msg);
  }

  [[nodiscard]] constexpr bool can_flush() const noexcept { return vtbl_ != nullptr && vtbl_->flush != nullptr; }

  void flush() noexcept {
    if (can_flush()) {
      vtbl_->flush(ctx_.get());
    }
  }

  void set_level(level l) noexcept { lvl_ = l; }
  [[nodiscard]] constexpr bool should_log(level l) const noexcept { return l >= lvl_; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return vtbl_ != nullptr; }

private:
  template <typename Tag> using ctx_t = typename extended_log_sink_traits<Tag>::context_type;

  template <typename Tag> static void log_entry(void *ctx, const extended_log_msg &msg) noexcept {
    extended_log_sink_traits<Tag>::log(value_ref<ctx_t<Tag>>(*static_cast<ctx_t<Tag> *>(ctx)), msg);
  }

  template <typename Tag> [[nodiscard]] static constexpr auto full_entry() noexcept {
    if constexpr (detail::has_extended_full<Tag>) {
      return +[](void *ctx, const extended_log_msg &msg) noexcept -> bool {
        return extended_log_sink_traits<Tag>::full(value_ref<ctx_t<Tag>>(*static_cast<ctx_t<Tag> *>(ctx)), msg);
      };
    } else {
      return static_cast<bool (*)(void *, const extended_log_msg &) noexcept>(nullptr);
    }
  }

  template <typename Tag> [[nodiscard]] static constexpr auto flush_entry() noexcept {
    if constexpr (detail::has_extended_flush<Tag>) {
      return +[](void *ctx) noexcept {
        extended_log_sink_traits<Tag>::flush(value_ref<ctx_t<Tag>>(*static_cast<ctx_t<Tag> *>(ctx)));
      };
    } else {
      return static_cast<void (*)(void *) noexcept>(nullptr);
    }
  }

  template <typename Tag> static constexpr vtable s_vtbl{&log_entry<Tag>, full_entry<Tag>(), flush_entry<Tag>()};

  value_ptr<void> ctx_{};
  const vtable *vtbl_{nullptr};
  level lvl_{level::trace};
};

template <std::size_t MaxSinks, typename Channel> struct fanout_log_sink_tag {};

/**
 * @brief Fixed-capacity sink that forwards each record to up to `MaxSinks`
 * advanced sinks ("pins"), applies back-pressure when one is full, and keeps
 * thread-context writers in FIFO order. Nothing here busy-waits.
 *
 * Two classes of writer:
 *
 *  - **Priority writers** (`meta.can_wait()` clear: IRQ, NMI, ...) never
 *    queue and never sleep. They write straight to every pin, which
 *    overwrites its oldest data or drops. They take no fan-out lock at all,
 *    so a sleeping thread writer can never delay them.
 *  - **Thread writers** (`meta.can_wait()` set) take a ticket under the
 *    channel lock and sleep on the channel until it is their turn (strict
 *    arrival order, one writer at a time). The turn holder then, if any pin
 *    is full, sleeps up to @ref set_wait_timeout_us for space and otherwise
 *    discards the record (counted in @ref dropped). It writes outside the
 *    channel lock and hands the turn on, waking the queue.
 *
 * Readers take only their pin's own lock; afterwards they call
 * @ref notify_space (thread context: it takes the channel lock) to wake the
 * writers. Lock order: channel lock, then pin lock. Call @ref notify_space
 * after releasing the pin lock.
 *
 * Pins must make `log` safe against concurrent priority writers (their own
 * short lock). A priority writer may refill a pin after the turn holder
 * saw space; the pin then drops that thread record (its `can_wait` is set).
 *
 * @code
 * // Two pins; thread writers sleep on a condition variable, FIFO.
 * microfmt::log::fanout_log_sink<2, microfmt::log::condition_variable_wait_channel> fan;
 * fan.add_sink(uart_ring_sink);          // advanced sinks, bound to their own contexts
 * fan.add_sink(flash_ring_sink);
 * fan.set_wait_timeout_us(5000);         // a thread waits up to 5 ms for space, per record
 *
 * microfmt::log::extended_logger<> logger("kern", fan.as_sink());
 *
 * // Consumer thread: drain one pin (under that pin's own lock), then:
 * fan.notify_space();                    // or hand fan.notifier() to the pin's reader
 * @endcode
 *
 * @tparam MaxSinks Capacity of the sink table.
 * @tparam Channel Outer lock + sleep/wake primitive, see `sync.hpp`.
 */
template <std::size_t MaxSinks, typename Channel = null_wait_channel> class fanout_log_sink {
  static_assert(MaxSinks > 0, "fanout_log_sink needs room for at least one sink");

  struct RELOCO_POINTER channel_guard {
    RELOCO_LOCK_IMPL explicit channel_guard(Channel &c) noexcept : c_(c) { c_.lock(); }
    ~channel_guard() noexcept { c_.unlock(); }
    channel_guard(const channel_guard &) = delete;
    channel_guard &operator=(const channel_guard &) = delete;
    Channel &c_;
  };

public:
  using tag = fanout_log_sink_tag<MaxSinks, Channel>;

  fanout_log_sink() noexcept = default;
  fanout_log_sink(const fanout_log_sink &) = delete;
  fanout_log_sink &operator=(const fanout_log_sink &) = delete;

  /**
   * @brief Appends @p s; returns `false` when the table is full. Thread
   * context only. Safe against concurrent writers of both classes.
   */
  bool add_sink(extended_log_sink s) noexcept {
    channel_guard g(channel_);
    const std::size_t n = count_.load(std::memory_order_relaxed);
    if (n >= MaxSinks) {
      return false;
    }
    sinks_[n] = s;
    count_.store(n + 1, std::memory_order_release); // publish to lock-free priority writers
    return true;
  }

  /** @brief Longest a turn holder sleeps for space, per record. */
  void set_wait_timeout_us(std::uint64_t us) noexcept { timeout_us_.store(us, std::memory_order_relaxed); }

  /** @brief Handle for use as a sink (e.g. by `extended_logger`). */
  [[nodiscard]] extended_log_sink as_sink(level lvl = level::trace) & noexcept RELOCO_LIFETIMEBOUND {
    return extended_log_sink(tag{}, *this, lvl);
  }

  /** @brief Reader side: tells waiting writers that space may have been freed. Thread context. */
  void notify_space() noexcept {
    channel_guard g(channel_);
    channel_.wake_all();
  }

  /** @brief @ref notify_space as a callback for a pin's consumer. */
  [[nodiscard]] space_notifier notifier() & noexcept RELOCO_LIFETIMEBOUND {
    return space_notifier(this, [](void *self) noexcept { static_cast<fanout_log_sink *>(self)->notify_space(); });
  }

  /** @brief Thread records discarded because a pin stayed full for the whole wait. */
  [[nodiscard]] std::size_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

  void log_impl(const extended_log_msg &msg) noexcept {
    if (!msg.meta.can_wait()) {
      write_through(msg);
      return;
    }

    bool accepted = false;
    {
      channel_guard g(channel_);
      const std::uint32_t ticket = next_ticket_++;
      // Strict arrival order: sleep (never spin) until every earlier writer finished.
      (void)channel_.wait_for(wait_forever, [this, ticket] { return serving_ == ticket; });
      accepted = !any_full(msg) || channel_.wait_for(timeout_us_.load(std::memory_order_relaxed),
                                                     [this, &msg] { return !any_full(msg); });
    }

    if (accepted) {
      write_through(msg);
    } else {
      dropped_.fetch_add(1, std::memory_order_relaxed);
    }

    channel_guard g(channel_);
    ++serving_;
    channel_.wake_all();
  }

  [[nodiscard]] bool full_impl(const extended_log_msg &msg) noexcept { return any_full(msg); }

  void flush_impl() noexcept {
    const std::size_t n = count_.load(std::memory_order_acquire);
    for (std::size_t i = 0; i < n; ++i) {
      sinks_[i].flush();
    }
  }

private:
  [[nodiscard]] bool any_full(const extended_log_msg &msg) noexcept {
    const std::size_t n = count_.load(std::memory_order_acquire);
    for (std::size_t i = 0; i < n; ++i) {
      if (sinks_[i].full(msg)) {
        return true;
      }
    }
    return false;
  }

  void write_through(const extended_log_msg &msg) noexcept {
    const std::size_t n = count_.load(std::memory_order_acquire);
    for (std::size_t i = 0; i < n; ++i) {
      sinks_[i].log(msg);
    }
  }

  Channel channel_{};
  microfmt::array<extended_log_sink, MaxSinks> sinks_{};
  std::atomic<std::size_t> count_{0};
  std::uint32_t next_ticket_{0}; // guarded by channel_
  std::uint32_t serving_{0};     // guarded by channel_
  std::atomic<std::uint64_t> timeout_us_{0};
  std::atomic<std::size_t> dropped_{0};
};

template <std::size_t MaxSinks, typename Channel>
struct extended_log_sink_traits<fanout_log_sink_tag<MaxSinks, Channel>> {
  using context_type = fanout_log_sink<MaxSinks, Channel>;

  static void log(value_ref<context_type> ctx, const extended_log_msg &msg) noexcept { ctx->log_impl(msg); }
  static bool full(value_ref<context_type> ctx, const extended_log_msg &msg) noexcept { return ctx->full_impl(msg); }
  static void flush(value_ref<context_type> ctx) noexcept { ctx->flush_impl(); }
};

} // namespace microfmt::log
