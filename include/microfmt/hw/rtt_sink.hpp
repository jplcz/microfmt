// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file rtt_sink.hpp
 * @brief Sink for SEGGER RTT (Real-Time Transfer): a ring buffer in target
 * RAM that a debug probe reads while the CPU keeps running.
 *
 * Why: RTT gives fast, non-intrusive console output over the debug probe with
 * no UART pins and almost no CPU cost. The probe software finds the control
 * block by scanning RAM for the 16-byte id `"SEGGER RTT"`, so the sink uses
 * the same control-block layout as SEGGER's own library. Pointers and sizes
 * follow the target's native width, as the probe expects.
 *
 * How: place one `rtt_sink` in static storage (it must stay at a fixed RAM
 * address and must not be moved), write through `as_sink()`, and attach the
 * probe. When the buffer is full the sink writes what fits and counts the
 * rest in `dropped_bytes()` (SEGGER's "trim" mode); it never blocks.
 */

#include "../microfmt.hpp"
#include "../reloco.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace microfmt {

/** @brief One RTT up (target to host) buffer descriptor, SEGGER-compatible layout. */
// NOLINTBEGIN(reloco-no-raw-pointer,reloco-banned-low-level): field types and order are dictated by the probe
struct RELOCO_POINTER rtt_up_buffer {
  const char *name{nullptr};
  char *buffer{nullptr};
  std::uint32_t size{0};
  std::uint32_t wr_off{0};          ///< Written by the target.
  volatile std::uint32_t rd_off{0}; ///< Written by the debug probe.
  std::uint32_t flags{0};
};
// NOLINTEND(reloco-no-raw-pointer,reloco-banned-low-level)

/** @brief RTT control block with a single up buffer and no down buffers. */
struct RELOCO_POINTER rtt_control_block {
  char id[16]{}; // NOLINT(reloco-no-c-array): the probe scans RAM for this exact 16-byte layout
  std::int32_t max_up_buffers{0};
  std::int32_t max_down_buffers{0};
  rtt_up_buffer up{};
};

/** @brief RTT sink with @p BufferSize bytes of up-buffer storage (one byte is always kept free). */
template <std::size_t BufferSize> class RELOCO_OWNER rtt_sink {
  static_assert(BufferSize > 1, "BufferSize must be at least 2 bytes");

public:
  rtt_sink() noexcept {
    storage_.fill(0);
    block_.max_up_buffers = 1;
    block_.max_down_buffers = 0;
    block_.up.name = "microfmt";
    block_.up.buffer = storage_.data();
    block_.up.size = static_cast<std::uint32_t>(BufferSize);
    block_.up.flags = 1; // trim: write what fits
    // The id is written last so a probe never sees a half-initialised block.
    constexpr string_view id = "SEGGER RTT";
    // the id field is a fixed C array because its layout is dictated by the probe.
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    for (std::size_t i = 0; i < id.size(); ++i) {
      block_.id[i] = id[i];
    }
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  rtt_sink(const rtt_sink &) = delete;
  rtt_sink &operator=(const rtt_sink &) = delete;

  /** @brief Returns a type-erased `microfmt::sink` view over this instance. */
  [[nodiscard]] sink as_sink() & noexcept RELOCO_LIFETIMEBOUND {
    return sink{this, [](void *ctx, string_view sv) noexcept { static_cast<rtt_sink *>(ctx)->write(sv); }};
  }

  /** @brief Copies as much of @p data as fits into the ring; counts the rest as dropped. */
  void write(string_view data) noexcept {
    const std::uint32_t size = block_.up.size;
    const std::uint32_t rd = block_.up.rd_off;
    const std::uint32_t wr = block_.up.wr_off;
    const std::size_t free_bytes = rd > wr ? rd - wr - 1 : size - 1 - (wr - rd);
    const std::size_t n = data.size() < free_bytes ? data.size() : free_bytes;
    dropped_ += data.size() - n;
    if (n == 0) {
      return;
    }
    std::uint32_t pos = wr;
    for (std::size_t i = 0; i < n; ++i) {
      storage_[pos] = data[i];
      pos = pos + 1 == size ? 0 : pos + 1;
    }
    // Make the bytes visible before the probe can see the new write offset.
    std::atomic_thread_fence(std::memory_order_release);
    block_.up.wr_off = pos;
  }

  /** @brief Number of bytes discarded because the probe had not drained the ring. */
  [[nodiscard]] std::size_t dropped_bytes() const noexcept { return dropped_; }

  /** @brief The control block (mutable so tests can play the probe by advancing `up.rd_off`). */
  [[nodiscard]] rtt_control_block &control_block() & noexcept RELOCO_LIFETIMEBOUND { return block_; }
  [[nodiscard]] const rtt_control_block &control_block() const & noexcept RELOCO_LIFETIMEBOUND { return block_; }
  const rtt_control_block &control_block() const && = delete;

  /** @brief Raw view of the up-buffer storage, for inspection. */
  [[nodiscard]] string_view storage() const & noexcept RELOCO_LIFETIMEBOUND {
    return string_view(storage_.data(), BufferSize);
  }
  string_view storage() const && = delete;

private:
  rtt_control_block block_{};
  microfmt::array<char, BufferSize> storage_{};
  std::size_t dropped_{0};
};

} // namespace microfmt
