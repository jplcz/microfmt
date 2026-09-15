// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file format_helpers.hpp @brief Hexadecimal, byte, address, and alignment helpers. */
#include "../microfmt.hpp"

namespace microfmt {

// ============================================================================
// Hex/Binary Explicit Wrapper
// ============================================================================

/**
 * @brief Explicitly formats an integer in hexadecimal or binary with
 * customizable prefix, padding width, and case without altering string
 * specifiers.
 */
template <typename T> struct hex_view {
  T value{};
  int width{0};
  bool prefix{false};
  bool uppercase{false};
};

template <typename T>
[[nodiscard]] constexpr hex_view<T> hex(T val, int width = 0,
                                        bool prefix = false,
                                        bool uppercase = false) noexcept {
  return hex_view<T>{val, width, prefix, uppercase};
}

template <typename T> struct formatter<hex_view<T>> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const hex_view<T> &h, const sink &out) const noexcept {
    if (h.prefix) {
      out.write(h.uppercase ? "0X" : "0x");
    }
    detail::format_unsigned(out, static_cast<uint64_t>(h.value), 16,
                            h.uppercase, h.width);
  }
};

/**
 * @brief Explicit binary integer formatter (e.g. for bitfield and control
 * register flags).
 */
template <typename T> struct bin_view {
  T value{};
  int width{sizeof(T) * 8};
  bool prefix{false};
};

template <typename T>
[[nodiscard]] constexpr bin_view<T> bin(T val, int width = sizeof(T) * 8,
                                        bool prefix = false) noexcept {
  return bin_view<T>{val, width, prefix};
}

template <typename T> struct formatter<bin_view<T>> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const bin_view<T> &b, const sink &out) const noexcept {
    if (b.prefix) {
      out.write("0b");
    }
    detail::format_unsigned(out, static_cast<uint64_t>(b.value), 2, false,
                            b.width);
  }
};

// ============================================================================
// Human-Readable Byte / Size Formatter
// ============================================================================

/**
 * @brief Automatically formats byte counts into human-readable units (B, KiB,
 * MiB, GiB, TiB).
 */
struct bytes_view {
  uint64_t bytes{0};
};

[[nodiscard]] constexpr bytes_view bytes(uint64_t count) noexcept {
  return bytes_view{count};
}

template <> struct formatter<bytes_view> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const bytes_view &b, const sink &out) const noexcept {
    constexpr const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    uint64_t val = b.bytes;
    size_t unit_idx = 0;
    uint64_t rem = 0;

    while (val >= 1024 && unit_idx < 5) {
      rem = (val % 1024) * 10 / 1024; // 1 decimal place remainder
      val /= 1024;
      unit_idx++;
    }

    detail::format_unsigned(out, val, 10, false, 0);
    if (unit_idx > 0 && rem > 0) {
      out.put('.');
      detail::format_unsigned(out, rem, 10, false, 0);
    }
    out.put(' ');
    out.write(units[unit_idx]);
  }
};

// ============================================================================
// Pointer Offset / Range Formatter
// ============================================================================

/**
 * @brief Formats an address with a base + offset view: `0x7fff0000+0x140`
 */
struct addr_offset_view {
  uintptr_t addr{0};
  uintptr_t base{0};
};

[[nodiscard]] constexpr addr_offset_view addr_offset(uintptr_t addr,
                                                     uintptr_t base) noexcept {
  return addr_offset_view{addr, base};
}

template <> struct formatter<addr_offset_view> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const addr_offset_view &v, const sink &out) const noexcept {
    out.write("0x");
    detail::format_unsigned(out, static_cast<uint64_t>(v.base), 16, false);
    if (v.addr >= v.base) {
      out.write("+0x");
      detail::format_unsigned(out, static_cast<uint64_t>(v.addr - v.base), 16,
                              false);
    } else {
      out.write("-0x");
      detail::format_unsigned(out, static_cast<uint64_t>(v.base - v.addr), 16,
                              false);
    }
  }
};

/**
 * @brief Formats an address range: `[0x10000000..0x10004000] (16 KiB)`
 */
struct memory_range_view {
  uintptr_t start{0};
  uintptr_t end{0};
};

[[nodiscard]] constexpr memory_range_view mem_range(uintptr_t start,
                                                    uintptr_t end) noexcept {
  return memory_range_view{start, end};
}

template <> struct formatter<memory_range_view> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const memory_range_view &r, const sink &out) const noexcept {
    out.write("[0x");
    detail::format_unsigned(out, static_cast<uint64_t>(r.start), 16, false);
    out.write("..0x");
    detail::format_unsigned(out, static_cast<uint64_t>(r.end), 16, false);
    out.write("] (");
    if (r.end >= r.start) {
      formatter<bytes_view>{}.format(bytes(r.end - r.start), out);
    } else {
      out.write("invalid");
    }
    out.put(')');
  }
};

// ============================================================================
// Padded / Aligned Text Field
// ============================================================================

enum class align_mode : uint8_t { left, right, center };

struct aligned_text_view {
  microfmt::string_view text{};
  size_t width{0};
  align_mode mode{align_mode::left};
  char pad_char{' '};
};

[[nodiscard]] constexpr aligned_text_view
align(microfmt::string_view text, size_t width, align_mode mode = align_mode::left,
      char pad_char = ' ') noexcept {
  return aligned_text_view{text, width, mode, pad_char};
}

template <> struct formatter<aligned_text_view> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const aligned_text_view &v, const sink &out) const noexcept {
    if (v.text.size() >= v.width) {
      out.write(v.text);
      return;
    }

    const size_t pad_total = v.width - v.text.size();
    switch (v.mode) {
    case align_mode::right:
      for (size_t i = 0; i < pad_total; ++i)
        out.put(v.pad_char);
      out.write(v.text);
      break;
    case align_mode::center: {
      const size_t left = pad_total / 2;
      const size_t right = pad_total - left;
      for (size_t i = 0; i < left; ++i)
        out.put(v.pad_char);
      out.write(v.text);
      for (size_t i = 0; i < right; ++i)
        out.put(v.pad_char);
      break;
    }
    case align_mode::left:
    default:
      out.write(v.text);
      for (size_t i = 0; i < pad_total; ++i)
        out.put(v.pad_char);
      break;
    }
  }
};

// ============================================================================
// Delimited Span / Array Formatter (Zero-Allocation Container Join)
// ============================================================================

/**
 * @brief Formats a contiguous array/span of values separated by a custom
 * delimiter. Example: `join(span, ", ")` -> `0x10, 0x20, 0x30`
 */
template <typename T> struct joined_span_view {
  span<const T> items{};
  microfmt::string_view delimiter{", "};
};

template <typename T>
[[nodiscard]] constexpr joined_span_view<T>
join(span<const T> items, microfmt::string_view delim = ", ") noexcept {
  return joined_span_view<T>{items, delim};
}

template <typename T> struct formatter<joined_span_view<T>> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const joined_span_view<T> &j, const sink &out) const noexcept {
    bool first = true;
    formatter<T> elem_fmt;
    format_parse_context dummy_ctx("");
    elem_fmt.parse(dummy_ctx);

    for (const auto &item : j.items) {
      if (!first) {
        out.write(j.delimiter);
      }
      first = false;
      elem_fmt.format(item, out);
    }
  }
};

} // namespace microfmt