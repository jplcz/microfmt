// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

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
using memory_reader_fn_t = size_t (*)(void *ctx, uintptr_t src_addr, uint8_t *dst_buf, size_t max_len) noexcept;

// ============================================================================
// Hex Dump Configuration & Descriptor
// ============================================================================

struct MICROFMT_API_CLASS hexdump_view {
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
[[nodiscard]] constexpr hexdump_view hexdump(span<const uint8_t> data, uintptr_t base_addr = 0,
                                             uint8_t bytes_per_line = 16, bool show_ascii = true) noexcept {
  return hexdump_view{base_addr,  data,        nullptr,
                      nullptr,    data.size(), bytes_per_line == 0 ? static_cast<uint8_t>(16) : bytes_per_line,
                      show_ascii, true,        false};
}

/**
 * @brief Helper for dumping memory via a fault-checked accessor probe.
 */
[[nodiscard]] constexpr hexdump_view hexdump_checked(uintptr_t base_addr, size_t total_len, memory_reader_fn_t reader,
                                                     void *ctx = nullptr, uint8_t bytes_per_line = 16,
                                                     bool show_ascii = true) noexcept {
  return hexdump_view{base_addr,  {},        ctx,
                      reader,     total_len, bytes_per_line == 0 ? static_cast<uint8_t>(16) : bytes_per_line,
                      show_ascii, true,      false};
}

// ============================================================================
// Formatter Specialization for hexdump_view
// ============================================================================

/**
 * @brief Formats a hex dump using caller-provided line storage.
 * @param h Dump descriptor.
 * @param line_buffer Reusable byte buffer for one rendered line.
 * @param out Destination sink.
 *
 * Out-of-line (see hexdump.ipp): the largest non-template free function in
 * the formatters/ tree the codesize testbed found still duplicated in full
 * across every shard .so in MICROFMT_SHARED mode (unlike int_formatter_specs
 * -- see microfmt.hpp/microfmt.ipp -- this one is an ordinary, non-template
 * function, so MICROFMT_API's declare-only/out-of-line split applies to it
 * directly, no overload-resolution trick needed).
 */
MICROFMT_API void format_hexdump(const hexdump_view &h, span<uint8_t> line_buffer, const sink &out) noexcept;

#if MICROFMT_SHARED_PROVIDE_DEFINITIONS
#include "hexdump.ipp"
#endif

template <> struct formatter<hexdump_view> {
  constexpr void parse(format_parse_context &ctx) noexcept { (void)ctx; }

  void format(const hexdump_view &h, const sink &out) const noexcept {
    uint8_t line_buffer[32]; // Bounded scratch buffer on stack
    format_hexdump(h, {line_buffer, sizeof(line_buffer)}, out);
  }
};

inline void hexdump_to(const sink &out, span<const uint8_t> data, uintptr_t base_address = 0) noexcept {
  formatter<hexdump_view> f;
  f.format(hexdump(data, base_address), out);
}

} // namespace microfmt