// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file memory_diff.hpp @brief Non-owning memory diff view and formatter for binary diagnostics. */

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "../microfmt.hpp"
#include "../reloco.hpp"

namespace microfmt {

/**
 * @brief Non-owning view representing a byte-level comparison between two memory regions.
 *
 * Designed for crash diagnostics, memory corruption tracing, and buffer delta inspection
 * without dynamic allocations or temporary string building.
 */
struct memory_diff_view {
  span<const std::byte> old_data;
  span<const std::byte> new_data;
  uintptr_t base_address{0};
  uint8_t bytes_per_row{16};
};

/**
 * @brief Creates a memory diff view over two @ref microfmt::span containers.
 *
 * @tparam T1 Element type of the baseline/old span.
 * @tparam T2 Element type of the modified/new span.
 * @param old_span Baseline memory span.
 * @param new_span Modified memory span.
 * @param base_addr Optional base address for absolute address annotation.
 * @return A lightweight @ref memory_diff_view instance.
 */
template <typename T1, typename T2>
[[nodiscard]] constexpr memory_diff_view mem_diff(span<const T1> old_span,
                                                  span<const std::byte> new_span, // Allow mixed or standard byte spans
                                                  uintptr_t base_addr = 0) noexcept {
  return memory_diff_view{
      span<const std::byte>(reinterpret_cast<const std::byte *>(old_span.data()), old_span.size_bytes()),
      span<const std::byte>(reinterpret_cast<const std::byte *>(new_span.data()), new_span.size_bytes()), base_addr, 16};
}

// Overload for matching template types
template <typename T1, typename T2>
[[nodiscard]] constexpr memory_diff_view mem_diff(span<const T1> old_span, span<const T2> new_span,
                                                  uintptr_t base_addr = 0) noexcept {
  return memory_diff_view{
      span<const std::byte>(reinterpret_cast<const std::byte *>(old_span.data()), old_span.size_bytes()),
      span<const std::byte>(reinterpret_cast<const std::byte *>(new_span.data()), new_span.size_bytes()), base_addr, 16};
}

#if RELOCO_HAS_STD_SPAN
/**
 * @brief Creates a memory diff view over two standard `std::span` containers.
 */
template <typename T1, typename T2, std::size_t Extent1, std::size_t Extent2>
[[nodiscard]] constexpr memory_diff_view mem_diff(std::span<const T1, Extent1> old_span,
                                                  std::span<const std::byte, Extent2> new_span,
                                                  uintptr_t base_addr = 0) noexcept {
  return memory_diff_view{
      span<const std::byte>(reinterpret_cast<const std::byte *>(old_span.data()), old_span.size_bytes()),
      span<const std::byte>(reinterpret_cast<const std::byte *>(new_span.data()), new_span.size_bytes()), base_addr,
      16};
}

template <typename T1, typename T2, std::size_t Extent1, std::size_t Extent2>
[[nodiscard]] constexpr memory_diff_view mem_diff(std::span<const T1, Extent1> old_span,
                                                  std::span<const T2, Extent2> new_span,
                                                  uintptr_t base_addr = 0) noexcept {
  return memory_diff_view{
      span<const std::byte>(reinterpret_cast<const std::byte *>(old_span.data()), old_span.size_bytes()),
      span<const std::byte>(reinterpret_cast<const std::byte *>(new_span.data()), new_span.size_bytes()), base_addr,
      16};
}
#endif

} // namespace microfmt

// ============================================================================
// Formatter Specialization
// ============================================================================

namespace microfmt {

template <> struct formatter<memory_diff_view> {
  constexpr void parse(format_parse_context &ctx) noexcept {
    (void)ctx; // Reserved for optional configuration specifiers (e.g. custom layout)
  }

  void format(const memory_diff_view &diff, const sink &out) const noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
    const size_t total_size = std::min(diff.old_data.size(), diff.new_data.size());
    const size_t row_size = (diff.bytes_per_row > 0) ? diff.bytes_per_row : 16;

    size_t identical_rows_streak = 0;
    size_t offset = 0;

    while (offset < total_size) {
      const size_t current_chunk_size = std::min(row_size, total_size - offset);

      // Compare old and new memory chunks for this row
      bool rows_match = true;
      for (size_t i = 0; i < current_chunk_size; ++i) {
        if (diff.old_data[offset + i] != diff.new_data[offset + i]) {
          rows_match = false;
          break;
        }
      }

      if (rows_match) {
        identical_rows_streak++;
      } else {
        // If we skipped matching rows prior to this divergence, collapse them into a summary line
        if (identical_rows_streak > 0) {
          out.write(microfmt::string_view("  [... "));
          detail::format_unsigned<detail::radix::decimal>(out, identical_rows_streak, false, 0);
          out.write(microfmt::string_view(" identical rows hidden ...]\n"));
          identical_rows_streak = 0;
        }

        // Emit Absolute Address Header: e.g. "  0x20000000: "
        out.write(microfmt::string_view("  0x"));
        detail::format_unsigned<detail::radix::hex>(out, diff.base_address + offset, false, 8);
        out.write(microfmt::string_view(": "));

        // Stream Old Bytes
        format_row_bytes(out, diff.old_data.subspan(offset, current_chunk_size));
        out.write(microfmt::string_view(" -> "));

        // Stream New Bytes
        format_row_bytes(out, diff.new_data.subspan(offset, current_chunk_size));
        out.write(microfmt::string_view("\n"));
      }

      offset += row_size;
    }

    // Flush any trailing identical row streaks at the end of the dump
    if (identical_rows_streak > 0) {
      out.write(microfmt::string_view("  [... "));
      detail::format_unsigned<detail::radix::decimal>(out, identical_rows_streak, false, 0);
      out.write(microfmt::string_view(" identical rows hidden ...]\n"));
    }
    RELOCO_END_UNSAFE_BUFFER_USAGE;
  }

private:
  static void format_row_bytes(const sink &out, span<const std::byte> chunk) noexcept {
    for (size_t i = 0; i < chunk.size(); ++i) {
      detail::format_unsigned<detail::radix::hex>(out, static_cast<uint64_t>(chunk[i]), true, 2);
      out.write(microfmt::string_view(" ", 1));
    }
  }
};

} // namespace microfmt