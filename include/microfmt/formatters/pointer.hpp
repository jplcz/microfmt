// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file pointer.hpp
 *  @brief Format raw addresses and contiguous pointer ranges deterministically.
 *
 *  Use @ref microfmt::raw_ptr for an address view with native, 32-bit, or
 *  64-bit compatibility widths. Use @ref microfmt::raw_range to render the
 *  values in a raw contiguous pointer range. Pointer specs support `p`, `P`,
 *  `x`, `X`, `32`, `64`, zero-padded widths, and `z`; range specs support
 *  `b`, `c`, and `n` and forward the remainder to each element.
 */

#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

namespace microfmt {

/** Address-width policy used by @ref raw_ptr_view. */
enum class ptr_width_mode : uint8_t {
  native = 0, // 8 hex digits on 32-bit arch, 16 on 64-bit arch
  compat32,   // Always 8 hex digits (32-bit address space, e.g. ARM Cortex-M /
              // RISC-V 32)
  compat64    // Always 16 hex digits
};

/** Non-owning raw address representation for formatting. */
struct raw_ptr_view {
  uintptr_t address{0};
  bool is_null{false};
  ptr_width_mode width_mode{ptr_width_mode::native};
};

/** Create a raw address view from a typed pointer. */
template <typename T>
/** Create a null raw address view. */
[[nodiscard]] constexpr raw_ptr_view
raw_ptr(const T *ptr, ptr_width_mode mode = ptr_width_mode::native) noexcept {
  return raw_ptr_view{reinterpret_cast<uintptr_t>(ptr), ptr == nullptr, mode};
}

/** Create a raw address view from an integer address. An address of zero is
 *  formatted as null. */
[[nodiscard]] constexpr raw_ptr_view
raw_ptr(std::nullptr_t, ptr_width_mode mode = ptr_width_mode::native) noexcept {
  return raw_ptr_view{0, true, mode};
}

[[nodiscard]] constexpr raw_ptr_view
raw_ptr(uintptr_t addr, ptr_width_mode mode = ptr_width_mode::native) noexcept {
  return raw_ptr_view{addr, addr == 0, mode};
}

/** Create a raw address view with an 8-hex-digit, 32-bit compatible width. */
template <typename T>
[[nodiscard]] constexpr raw_ptr_view raw_ptr32(const T *ptr) noexcept {
  return raw_ptr(ptr, ptr_width_mode::compat32);
}

/** Create a 32-bit-compatible null raw address view. */
[[nodiscard]] constexpr raw_ptr_view raw_ptr32(std::nullptr_t) noexcept {
  return raw_ptr(nullptr, ptr_width_mode::compat32);
}

/** Create a 32-bit-compatible raw address view from an integer address. */
[[nodiscard]] constexpr raw_ptr_view raw_ptr32(uintptr_t addr) noexcept {
  return raw_ptr(addr, ptr_width_mode::compat32);
}

/** Non-owning contiguous range of values addressed by raw pointers. */
template <typename T> struct raw_range_view {
  const T *begin_ptr{nullptr};
  const T *end_ptr{nullptr};
  std::string_view separator{", "};
  char open_delim{'['};
  char close_delim{']'};

  /** Return the number of elements when both pointers refer to the same array
   *  (or one-past its end); returns zero for a reversed range. */
  [[nodiscard]] constexpr size_t size() const noexcept {
    if (begin_ptr == nullptr || end_ptr == nullptr || end_ptr < begin_ptr) {
      return 0;
    }
    return static_cast<size_t>(end_ptr - begin_ptr);
  }
};

/** Create a value range from begin and one-past-end pointers. Both pointers
 *  must designate positions within the same array. */
template <typename T>
[[nodiscard]] constexpr raw_range_view<T> raw_range(const T *first,
                                                    const T *last) noexcept {
  return raw_range_view<T>{first, last};
}

/** Create a value range from a pointer and element count. A null pointer
 *  produces an empty range. */
template <typename T>
[[nodiscard]] constexpr raw_range_view<T> raw_range(const T *first,
                                                    size_t count) noexcept {
  return raw_range_view<T>{first, first ? first + count : nullptr};
}

/** Formatter for @ref raw_ptr_view.
 *
 *  `p` and `P` emit lower- or upper-case hexadecimal with `0x`/`0X` prefixes;
 *  `x` and `X` omit those prefixes. `32` and `64` select fixed compatibility
 *  widths, `0N` selects a minimum N-digit width, and `z` renders null as
 *  `0x0` instead of `(nil)`.
 */
template <> struct formatter<raw_ptr_view> {
  static constexpr size_t native_hex_width = sizeof(uintptr_t) * 2;

  bool uppercase_hex{false};
  bool show_prefix{true};
  ptr_width_mode mode{ptr_width_mode::native};
  size_t custom_width{0};
  std::string_view null_representation{"(nil)"};

  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    if (spec.empty())
      return;

    size_t i = 0;
    while (i < spec.size()) {
      char c = spec[i];
      if (c == '3' && (i + 1) < spec.size() && spec[i + 1] == '2') {
        // Specifier "32" -> force compat 32-bit mode
        mode = ptr_width_mode::compat32;
        i += 2;
        continue;
      } else if (c == '6' && (i + 1) < spec.size() && spec[i + 1] == '4') {
        // Specifier "64" -> force 64-bit mode
        mode = ptr_width_mode::compat64;
        i += 2;
        continue;
      } else if (c == 'p') {
        uppercase_hex = false;
        show_prefix = true;
      } else if (c == 'P') {
        uppercase_hex = true;
        show_prefix = true;
      } else if (c == 'x') {
        uppercase_hex = false;
        show_prefix = false;
      } else if (c == 'X') {
        uppercase_hex = true;
        show_prefix = false;
      } else if (c == '0' && (i + 1) < spec.size() && spec[i + 1] >= '0' &&
                 spec[i + 1] <= '9') {
        // Explicit width padding: {:08x}, {:016p}
        size_t w = 0;
        ++i;
        while (i < spec.size() && spec[i] >= '0' && spec[i] <= '9') {
          w = w * 10 + static_cast<size_t>(spec[i] - '0');
          ++i;
        }
        custom_width = w;
        --i;
      } else if (c == 'z') { // 'z' -> format nullptr as 0x0
        null_representation = "0x0";
      }
      ++i;
    }
  }

  void format(const raw_ptr_view &ptr, const sink &out) const noexcept {
    if (ptr.is_null) {
      out.write(null_representation);
      return;
    }

    if (show_prefix) {
      out.write(uppercase_hex ? "0X" : "0x");
    }

    // Determine target width
    size_t width = native_hex_width;
    const ptr_width_mode effective_mode =
        (mode != ptr_width_mode::native) ? mode : ptr.width_mode;

    if (custom_width > 0) {
      width = custom_width;
    } else if (effective_mode == ptr_width_mode::compat32) {
      width = 8;
    } else if (effective_mode == ptr_width_mode::compat64) {
      width = 16;
    }

    // Mask to 32 bits if compat32
    uint64_t addr = static_cast<uint64_t>(ptr.address);
    if (effective_mode == ptr_width_mode::compat32) {
      addr &= 0xFFFFFFFFULL;
    }

    const int output_width =
        width > static_cast<size_t>(std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max()
            : static_cast<int>(width);
    detail::format_unsigned(out, addr, 16, uppercase_hex, output_width);
  }
};

/** Formatter for @ref raw_range_view.
 *
 *  `b` writes square brackets, `c` writes braces, and `n` omits outer
 *  delimiters. The remaining specifier is forwarded to every range element.
 */
template <typename T> struct formatter<raw_range_view<T>> {
  char open_c{'['};
  char close_c{']'};
  std::string_view forwarded_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    if (spec.empty())
      return;

    size_t i = 0;
    if (spec[0] == 'b' || spec[0] == 'B') {
      open_c = '[';
      close_c = ']';
      i = 1;
    } else if (spec[0] == 'c' || spec[0] == 'C') {
      open_c = '{';
      close_c = '}';
      i = 1;
    } else if (spec[0] == 'n' || spec[0] == 'N') {
      open_c = '\0';
      close_c = '\0';
      i = 1;
    }

    forwarded_spec = spec.substr(i);
  }

  void format(const raw_range_view<T> &range, const sink &out) const noexcept {
    if (open_c != '\0') {
      out.put(open_c);
    }

    formatter<T> elem_fmt;
    format_parse_context elem_ctx(forwarded_spec);
    elem_fmt.parse(elem_ctx);

    const size_t count = range.size();
    for (size_t idx = 0; idx < count; ++idx) {
      if (idx > 0) {
        out.write(range.separator);
      }
      elem_fmt.format(range.begin_ptr[idx], out);
    }

    if (close_c != '\0') {
      out.put(close_c);
    }
  }
};

} // namespace microfmt