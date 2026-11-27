// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file async_queue_sink.hpp
 * @brief Wait-free single-producer single-consumer (SPSC) byte queue sink.
 *
 * Why: writing to a slow destination (flash, UART, network) from a time-critical
 * context such as an interrupt handler or real-time task would stall it. This
 * sink only copies bytes into a RAM queue, which is quick and never waits;
 * a different context (an idle loop or a low-priority task) later calls
 * `drain()` to push the queued bytes to the slow destination.
 *
 * How: exactly one context may call `write()` (the producer) and exactly one
 * may call `drain()` (the consumer); they can run concurrently without locks.
 * When the queue is full the bytes that do not fit are dropped and counted in
 * `dropped_bytes()`, so the producer is never blocked. Unlike
 * `ring_buffer_sink`, which overwrites old data for post-mortem dumps, this
 * queue keeps the oldest data and is emptied by reading.
 */

#include "../microfmt.hpp"
#include "../reloco.hpp"
#include <atomic>
#include <cstddef>

namespace microfmt {

/** @brief SPSC queue sink holding up to @p Capacity bytes (a non-zero power of two). */
template <std::size_t Capacity> class async_queue_sink {
  static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a non-zero power of two");

public:
  async_queue_sink() noexcept = default;
  async_queue_sink(const async_queue_sink &) = delete;
  async_queue_sink &operator=(const async_queue_sink &) = delete;

  /** @brief Returns a type-erased `microfmt::sink` view; use it from the producer context only. */
  [[nodiscard]] sink as_sink() & noexcept RELOCO_LIFETIMEBOUND {
    return sink{this, [](void *ctx, string_view sv) noexcept { static_cast<async_queue_sink *>(ctx)->write(sv); }};
  }

  /** @brief Producer: queues as much of @p data as fits and counts the rest as dropped. */
  void write(string_view data) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    const std::size_t free_bytes = Capacity - (head - tail);
    const std::size_t n = data.size() < free_bytes ? data.size() : free_bytes;
    for (std::size_t i = 0; i < n; ++i) {
      buffer_[(head + i) & Mask] = data[i];
    }
    if (n < data.size()) {
      dropped_.fetch_add(data.size() - n, std::memory_order_relaxed);
    }
    head_.store(head + n, std::memory_order_release);
  }

  /** @brief Consumer: writes up to @p max_bytes queued bytes to @p target in order; returns the count. */
  std::size_t drain(const sink &target, std::size_t max_bytes = Capacity) noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t head = head_.load(std::memory_order_acquire);
    std::size_t n = head - tail;
    n = n < max_bytes ? n : max_bytes;
    const std::size_t start = tail & Mask;
    const std::size_t first = n < Capacity - start ? n : Capacity - start;
    if (first > 0) {
      target.write(string_view(buffer_.data(), Capacity).substr(start, first));
    }
    if (n > first) {
      target.write(string_view(buffer_.data(), Capacity).substr(0, n - first));
    }
    tail_.store(tail + n, std::memory_order_release);
    return n;
  }

  /** @brief Bytes currently queued (exact only when producer and consumer are quiescent). */
  [[nodiscard]] std::size_t size() const noexcept {
    return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
  }

  /** @brief Bytes discarded because the queue was full. */
  [[nodiscard]] std::size_t dropped_bytes() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
  static constexpr std::size_t Mask = Capacity - 1;

  microfmt::array<char, Capacity> buffer_{};
  std::atomic<std::size_t> head_{0}; ///< Total bytes ever queued (producer-owned).
  std::atomic<std::size_t> tail_{0}; ///< Total bytes ever drained (consumer-owned).
  std::atomic<std::size_t> dropped_{0};
};

} // namespace microfmt
