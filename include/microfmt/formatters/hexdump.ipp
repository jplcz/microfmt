// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file hexdump.ipp @brief Out-of-line body for format_hexdump() (see
 * hexdump.hpp). Included from hexdump.hpp itself, guarded on
 * MICROFMT_SHARED_PROVIDE_DEFINITIONS (see microfmt/detail/compat.hpp): once
 * in every header-only build, and once more from whichever single
 * translation unit builds a MICROFMT_SHARED library (see
 * microfmt_compile.hpp). Never included directly. */

MICROFMT_API void format_hexdump(const hexdump_view &h, span<uint8_t> line_buffer, const sink &out) noexcept {
  if (h.total_len == 0 || line_buffer.empty()) {
    return;
  }

  const size_t requested_bpl = h.bytes_per_line == 0 ? 16 : h.bytes_per_line;
  const size_t bpl = std::min<size_t>(std::min<size_t>(requested_bpl, 32), line_buffer.size());

  size_t offset = 0;
  while (offset < h.total_len) {
    const size_t chunk_len = std::min(bpl, h.total_len - offset);
    const uintptr_t current_addr = h.base_addr + offset;
    size_t bytes_valid = 0;

    // Read memory into line buffer safely
    if (h.reader_fn != nullptr) {
      bytes_valid = h.reader_fn(h.reader_ctx, current_addr, line_buffer.data(), chunk_len);
    } else if (!h.direct_data.empty()) {
      const size_t avail = (offset < h.direct_data.size()) ? (h.direct_data.size() - offset) : 0;
      bytes_valid = std::min(chunk_len, avail);
      if (bytes_valid > 0) {
        RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
        std::copy_n(h.direct_data.data() + offset, bytes_valid, line_buffer.data());
        RELOCO_END_UNSAFE_BUFFER_USAGE;
      }
    }

    // Format Address Prefix
    if (h.show_address) {
      if constexpr (sizeof(uintptr_t) == 8) {
        detail::format_unsigned<detail::radix::hex>(out, static_cast<uint64_t>(current_addr), h.uppercase, 16);
      } else {
        detail::format_unsigned<detail::radix::hex>(out, static_cast<uint64_t>(current_addr), h.uppercase, 8);
      }
      out.write(": ");
    }

    // Format Hex Octets
    for (size_t i = 0; i < bpl; ++i) {
      if (i < bytes_valid) {
        detail::format_unsigned<detail::radix::hex>(out, line_buffer[i], h.uppercase, 2);
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
