// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

/** @file hexdump.hpp @brief Direct and fault-checked memory hex-dump views. */

#include "../microfmt.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace microfmt {

// ============================================================================
// Checked Memory Reader Callback Definition
// ============================================================================

/**
 * @brief Function signature for a safe/probed memory reader.
 *
 * @param ctx       Opaque context pointer (e.g. page table or VM handle).
 * @param src_addr  Virtual or physical target address to read from.
 * @param dst_buf   Local destination buffer to fill.
 * @param max_len   Number of bytes requested.
 * @return size_t   Number of bytes successfully read without faulting (0 on
 * error).
 */
using memory_reader_fn_t = size_t (*)(void *ctx, uintptr_t src_addr,
                                      uint8_t *dst_buf,
                                      size_t max_len) noexcept;

// ============================================================================
// Hex Dump Configuration & Descriptor
// ============================================================================

struct hexdump_view {
  uintptr_t base_addr{0};
  span<const uint8_t> direct_data{};
  void *reader_ctx{nullptr};
  memory_reader_fn_t reader_fn{nullptr};
  size_t total_len{0};
  uint8_t bytes_per_line{16};
  bool show_ascii{true};
  bool show_address{true};
  bool uppercase{false};
};

/**
 * @brief Helper for dumping safe, direct in-memory buffers (zero overhead).
 */
[[nodiscard]] constexpr hexdump_view hexdump(span<const uint8_t> data,
                                             uintptr_t base_addr = 0,
                                             uint8_t bytes_per_line = 16,
                                             bool show_ascii = true) noexcept {
  return hexdump_view{
      base_addr,
      data,
      nullptr,
      nullptr,
      data.size(),
      bytes_per_line == 0 ? static_cast<uint8_t>(16) : bytes_per_line,
      show_ascii,
      true,
      false};
}

/**
 * @brief Helper for dumping memory via a fault-checked accessor probe.
 */
[[nodiscard]] constexpr hexdump_view
hexdump_checked(uintptr_t base_addr, size_t total_len,
                memory_reader_fn_t reader, void *ctx = nullptr,
                uint8_t bytes_per_line = 16, bool show_ascii = true) noexcept {
  return hexdump_view{
      base_addr,
      {},
      ctx,
      reader,
      total_len,
      bytes_per_line == 0 ? static_cast<uint8_t>(16) : bytes_per_line,
      show_ascii,
      true,
      false};
}

// ============================================================================
// Formatter Specialization for hexdump_view
// ============================================================================

/**
 * @brief Formats a hex dump using caller-provided line storage.
 * @param h Dump descriptor.
 * @param line_buffer Reusable byte buffer for one rendered line.
 * @param out Destination sink.
 */
inline void format_hexdump(const hexdump_view &h, span<uint8_t> line_buffer,
                           const sink &out) noexcept {
  if (h.total_len == 0 || line_buffer.empty()) {
    return;
  }

  const size_t requested_bpl =
      h.bytes_per_line == 0 ? 16 : h.bytes_per_line;
  const size_t bpl =
      std::min<size_t>(std::min<size_t>(requested_bpl, 32),
                       line_buffer.size());

  size_t offset = 0;
  while (offset < h.total_len) {
    const size_t chunk_len = std::min(bpl, h.total_len - offset);
    const uintptr_t current_addr = h.base_addr + offset;
    size_t bytes_valid = 0;

    // Read memory into line buffer safely
    if (h.reader_fn != nullptr) {
      bytes_valid =
          h.reader_fn(h.reader_ctx, current_addr, line_buffer.data(), chunk_len);
    } else if (!h.direct_data.empty()) {
      const size_t avail = (offset < h.direct_data.size())
                               ? (h.direct_data.size() - offset)
                               : 0;
      bytes_valid = std::min(chunk_len, avail);
      if (bytes_valid > 0) {
        std::copy_n(h.direct_data.data() + offset, bytes_valid,
                    line_buffer.data());
      }
    }

    // Format Address Prefix
    if (h.show_address) {
      if constexpr (sizeof(uintptr_t) == 8) {
        detail::format_unsigned(out, static_cast<uint64_t>(current_addr), 16,
                                h.uppercase, 16);
      } else {
        detail::format_unsigned(out, static_cast<uint64_t>(current_addr), 16,
                                h.uppercase, 8);
      }
      out.write(": ");
    }

    // Format Hex Octets
    for (size_t i = 0; i < bpl; ++i) {
      if (i < bytes_valid) {
        detail::format_unsigned(out, line_buffer[i], 16, h.uppercase, 2);
        out.put(' ');
      } else if (i < chunk_len) {
        // Memory probed but unmapped / fault occurred
        out.write("?? ");
      } else {
        // Padding past EOF
        out.write("   ");
      }

      // Split half-line for visual readability (8-byte separator)
      if (i == 7 && bpl > 8) {
        out.put(' ');
      }
    }

    // Format ASCII Pane
    if (h.show_ascii) {
      out.write(" |");
      for (size_t i = 0; i < chunk_len; ++i) {
        if (i < bytes_valid) {
          const uint8_t b = line_buffer[i];
          out.put((b >= 32 && b <= 126) ? static_cast<char>(b) : '.');
        } else {
          out.put('?');
        }
      }
      for (size_t i = chunk_len; i < bpl; ++i) {
        out.put(' ');
      }
      out.put('|');
    }

    offset += chunk_len;
    out.put('\n');
  }
}

template <> struct formatter<hexdump_view> {
  constexpr void parse(format_parse_context &ctx) noexcept { (void)ctx; }

  void format(const hexdump_view &h, const sink &out) const noexcept {
    uint8_t line_buffer[32]; // Bounded scratch buffer on stack
    format_hexdump(h, {line_buffer, sizeof(line_buffer)}, out);
  }
};

inline void hexdump_to(const sink &out, span<const uint8_t> data,
                       uintptr_t base_address = 0) noexcept {
  formatter<hexdump_view> f;
  f.format(hexdump(data, base_address), out);
}

} // namespace microfmt