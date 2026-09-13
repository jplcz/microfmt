#pragma once

/** @file ring_buffer_sink.hpp @brief Fixed-capacity circular output-buffer sink. */

#include "../microfmt.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace microfmt {

// ============================================================================
// Zero-Allocation Ring Buffer Sink
// ============================================================================

template <size_t Capacity> class ring_buffer_sink {
  static_assert(Capacity > 0, "Capacity must be greater than 0");
  static_assert((Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of 2 for fast bitmask wrapping");

public:
  constexpr ring_buffer_sink() noexcept = default;

  // Non-copyable, non-movable to ensure safe raw pointer context binding
  ring_buffer_sink(const ring_buffer_sink &) = delete;
  ring_buffer_sink &operator=(const ring_buffer_sink &) = delete;

  // ==========================================================================
  // microfmt::sink Bridge
  // ==========================================================================

  [[nodiscard]] sink as_sink() noexcept {
    return sink{this, [](void *ctx, std::string_view sv) noexcept {
                  static_cast<ring_buffer_sink *>(ctx)->write(sv);
                }};
  }

  // ==========================================================================
  // Writing (Overwrite on overflow)
  // ==========================================================================

  void put(char c) noexcept {
    const size_t pos = head_.fetch_add(1, std::memory_order_relaxed);
    buffer_[pos & Mask] = c;
    count_.fetch_add(1, std::memory_order_relaxed);
  }

  void write(std::string_view sv) noexcept {
    for (char c : sv) {
      put(c);
    }
  }

  // ==========================================================================
  // Status & Sizing
  // ==========================================================================

  [[nodiscard]] size_t size() const noexcept {
    const size_t c = count_.load(std::memory_order_relaxed);
    return c > Capacity ? Capacity : c;
  }

  [[nodiscard]] constexpr size_t capacity() const noexcept { return Capacity; }
  [[nodiscard]] bool empty() const noexcept { return size() == 0; }
  [[nodiscard]] bool full() const noexcept {
    return count_.load(std::memory_order_relaxed) >= Capacity;
  }

  void reset() noexcept {
    head_.store(0, std::memory_order_relaxed);
    count_.store(0, std::memory_order_relaxed);
  }

  // ==========================================================================
  // Crash Dump / Post-Mortem Slices
  // ==========================================================================

  struct dump_slices {
    std::string_view first;
    std::string_view second; // Empty if buffer hasn't wrapped
  };

  // Inspects data chronologically from oldest to newest without copying
  [[nodiscard]] dump_slices view() const noexcept {
    const size_t total_written = count_.load(std::memory_order_relaxed);
    if (total_written == 0) {
      return {};
    }

    if (total_written <= Capacity) {
      // Linear layout (not yet wrapped)
      return {std::string_view(buffer_.data(), total_written), {}};
    }

    // Wrapped: oldest data starts at (head_ & Mask)
    const size_t tail = head_.load(std::memory_order_relaxed) & Mask;
    const size_t first_len = Capacity - tail;
    const size_t second_len = tail;

    return {std::string_view(buffer_.data() + tail, first_len),
            std::string_view(buffer_.data(), second_len)};
  }

  // Flushes full ring buffer content chronologically to an external sink
  void dump_to(const sink &target) const noexcept {
    const auto slices = view();
    if (!slices.first.empty()) {
      target.write(slices.first);
    }
    if (!slices.second.empty()) {
      target.write(slices.second);
    }
  }

private:
  static constexpr size_t Mask = Capacity - 1;

  alignas(void *) std::array<char, Capacity> buffer_{};
  std::atomic<size_t> head_{0};
  std::atomic<size_t> count_{0};
};

} // namespace microfmt
