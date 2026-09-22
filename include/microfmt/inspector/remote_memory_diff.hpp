// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file remote_memory_diff.hpp @brief Remote address space memory diff view and formatter. */

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "address_space.hpp"

namespace microfmt {

/**
 * @brief Non-owning view representing a byte-level comparison between two remote address spaces.
 */
struct remote_memory_diff_view {
  address_space_ref old_space;
  uintptr_t old_address;

  address_space_ref new_space;
  uintptr_t new_address;

  size_t size;
  span<std::byte> scratch; // Caller-owned scratch buffer for safe chunk reads
  uint8_t bytes_per_row{16};
};

/**
 * @brief Creates a remote memory diff view.
 */
[[nodiscard]] constexpr remote_memory_diff_view remote_mem_diff(address_space_ref old_space, uintptr_t old_addr,
                                                                address_space_ref new_space, uintptr_t new_addr,
                                                                size_t size, span<std::byte> scratch_buffer) noexcept {
  return remote_memory_diff_view{old_space, old_addr, new_space, new_addr, size, scratch_buffer, 16};
}

} // namespace microfmt

// ============================================================================
// Formatter Specialization
// ============================================================================

namespace microfmt {

template <> struct formatter<remote_memory_diff_view> {
  constexpr void parse(format_parse_context &ctx) noexcept { (void)ctx; }

  void format(const remote_memory_diff_view &diff, const sink &out) const noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

    // Scratch buffer must be at least large enough to hold two rows (old and new chunks)
    const size_t row_size = (diff.bytes_per_row > 0) ? diff.bytes_per_row : 16;
    if (diff.scratch.size() < row_size * 2) {
      out.write(string_view("[ERROR: Scratch buffer too small for remote_memory_diff]"));
      return;
    }

    std::byte *old_chunk_buf = diff.scratch.data();
    std::byte *new_chunk_buf = diff.scratch.data() + row_size;

    size_t identical_rows_streak = 0;
    size_t offset = 0;

    while (offset < diff.size) {
      const size_t current_chunk_size = std::min(row_size, diff.size - offset);

      // Read remote/target chunks safely using address_space_ref
      auto old_read = diff.old_space.read_bytes(diff.old_address + offset, old_chunk_buf, current_chunk_size);
      auto new_read = diff.new_space.read_bytes(diff.new_address + offset, new_chunk_buf, current_chunk_size);

      if (!old_read || !new_read) {
        // Handle read fault gracefully
        if (identical_rows_streak > 0) {
          flush_streak(out, identical_rows_streak);
          identical_rows_streak = 0;
        }
        out.write(string_view("  0x"));
        detail::format_unsigned<detail::radix::hex>(out, diff.new_address + offset, false, 8);
        out.write(string_view(": [REMOTE READ FAULT]\n"));
        offset += row_size;
        continue;
      }

      span<const std::byte> old_chunk(old_chunk_buf, current_chunk_size);
      span<const std::byte> new_chunk(new_chunk_buf, current_chunk_size);

      // Compare chunks
      bool rows_match = true;
      for (size_t i = 0; i < current_chunk_size; ++i) {
        if (old_chunk[i] != new_chunk[i]) {
          rows_match = false;
          break;
        }
      }

      if (rows_match) {
        identical_rows_streak++;
      } else {
        if (identical_rows_streak > 0) {
          flush_streak(out, identical_rows_streak);
          identical_rows_streak = 0;
        }

        // Print Address Header
        out.write(string_view("  0x"));
        detail::format_unsigned<detail::radix::hex>(out, diff.new_address + offset, false, 8);
        out.write(string_view(": "));

        format_row_bytes(out, old_chunk);
        out.write(string_view(" -> "));
        format_row_bytes(out, new_chunk);
        out.write(string_view("\n"));
      }

      offset += row_size;
    }

    if (identical_rows_streak > 0) {
      flush_streak(out, identical_rows_streak);
    }

    RELOCO_END_UNSAFE_BUFFER_USAGE;
  }

private:
  static void flush_streak(const sink &out, size_t count) noexcept {
    out.write(string_view("  [... "));
    detail::format_unsigned<detail::radix::decimal>(out, count, false, 0);
    out.write(string_view(" identical rows hidden ...]\n"));
  }

  static void format_row_bytes(const sink &out, span<const std::byte> chunk) noexcept {
    for (size_t i = 0; i < chunk.size(); ++i) {
      detail::format_unsigned<detail::radix::hex>(out, static_cast<uint64_t>(chunk[i]), true, 2);
      out.write(string_view(" ", 1));
    }
  }
};

} // namespace microfmt1