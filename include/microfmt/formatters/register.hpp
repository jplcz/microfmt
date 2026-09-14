// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace microfmt {

// ============================================================================
// Static Bitfield / Register Descriptors (Placed in Flash / .rodata)
// ============================================================================

struct reg_field {
  std::string_view name;
  uint8_t bit_offset;
  uint8_t bit_width;
  bool is_flag{
      false}; // If true (width=1), format as +FLAG / -FLAG or flag name only
};

template <size_t N> struct reg_descriptor {
  std::string_view name;
  size_t byte_width{4}; // 1, 2, 4, or 8 bytes
  reg_field fields[N];
};

// Deduction guide
template <typename... Fields>
reg_descriptor(std::string_view, size_t, Fields...)
    -> reg_descriptor<sizeof...(Fields)>;

// ============================================================================
// Register Value + Descriptor View
// ============================================================================

template <size_t N, typename UInt = uint32_t> struct reg_view {
  UInt value;
  const reg_descriptor<N> *desc{nullptr};
};

template <size_t N, typename UInt>
[[nodiscard]] constexpr auto
format_reg(UInt val, const reg_descriptor<N> &desc) noexcept {
  return reg_view<N, UInt>{val, &desc};
}

// ============================================================================
// Formatter for Register View
// ============================================================================

template <size_t N, typename UInt> struct formatter<reg_view<N, UInt>> {
  bool detailed{
      true};         // 'd' -> detailed fields, 's' -> short (active flags only)
  bool naked{false}; // 'n' -> no outer braces

  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    for (char c : spec) {
      if (c == 's' || c == 'S')
        detailed = false;
      else if (c == 'n' || c == 'N')
        naked = true;
    }
  }

  void format(const reg_view<N, UInt> &rv, const sink &out) const noexcept {
    if (!rv.desc)
      return;
    const auto &d = *rv.desc;

    // Register Name & Raw Value
    out.write(d.name);
    out.write("=0x");
    detail::format_unsigned(out, static_cast<uint64_t>(rv.value), 16, true,
                            d.byte_width * 2);

    // Decode Bitfields
    if (!naked)
      out.write(" [");
    else
      out.put(' ');

    size_t printed_count = 0;
    for (size_t i = 0; i < N; ++i) {
      const auto &f = d.fields[i];
      const uint64_t mask =
          (f.bit_width == 64) ? ~0ULL : ((1ULL << f.bit_width) - 1ULL);
      const uint64_t val =
          (static_cast<uint64_t>(rv.value) >> f.bit_offset) & mask;

      if (f.is_flag || f.bit_width == 1) {
        if (!detailed && val == 0) {
          continue; // Short mode: skip inactive flags
        }
        if (printed_count++ > 0)
          out.write(", ");

        if (val) {
          out.write(f.name);
        } else {
          out.put('!');
          out.write(f.name);
        }
      } else {
        if (!detailed && val == 0) {
          continue; // Short mode: skip zeroed fields
        }
        if (printed_count++ > 0)
          out.write(", ");

        out.write(f.name);
        out.put('=');
        if (val > 9) {
          out.write("0x");
          detail::format_unsigned(out, val, 16, false, 0);
        } else {
          detail::format_unsigned(out, val, 10, false, 0);
        }
      }
    }

    if (!naked)
      out.put(']');
  }
};

} // namespace microfmt