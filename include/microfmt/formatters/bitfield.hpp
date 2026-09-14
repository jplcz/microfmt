// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

/** @file bitfield.hpp @brief Named bitfield and register-value formatting views. */

#include "../microfmt.hpp"
#include <cstdint>
#include <string_view>

namespace microfmt {

enum class bit_type : uint8_t {
  flag,      // Single bit or multi-bit exact match (prints name when set)
  value_hex, // Multi-bit field (prints NAME=0xVAL)
  value_dec  // Multi-bit field (prints NAME=VAL)
};

struct bit_field {
  uint32_t mask;
  uint8_t shift{0};
  std::string_view name{};
  bit_type type{bit_type::flag};
};

struct bitfield_view {
  uint32_t raw_value{0};
  span<const bit_field> fields{};
  std::string_view separator{" | "};
  bool show_raw_hex{true};
};

// Convenience helper to construct a bitfield_view
[[nodiscard]] constexpr bitfield_view
bits(uint32_t raw_val, span<const bit_field> fields, bool show_raw_hex = true,
     std::string_view sep = " | ") noexcept {
  return bitfield_view{raw_val, fields, sep, show_raw_hex};
}

template <> struct formatter<bitfield_view> {
  constexpr void parse(format_parse_context &ctx) noexcept { (void)ctx; }

  void format(const bitfield_view &bv, const sink &out) const noexcept {
    if (bv.show_raw_hex) {
      out.write("0x");
      detail::format_unsigned(out, bv.raw_value, 16, false, 8);
      out.write(" [");
    } else {
      out.put('[');
    }

    bool first = true;
    for (const auto &f : bv.fields) {
      if (f.mask == 0)
        continue;

      const uint32_t extracted = (bv.raw_value & f.mask) >> f.shift;

      if (f.type == bit_type::flag) {
        if ((bv.raw_value & f.mask) == f.mask) {
          if (!first)
            out.write(bv.separator);
          first = false;
          out.write(f.name);
        }
      } else {
        // Multi-bit value field: only format if non-zero
        if (extracted != 0) {
          if (!first)
            out.write(bv.separator);
          first = false;

          out.write(f.name);
          out.put('=');
          if (f.type == bit_type::value_hex) {
            out.write("0x");
            detail::format_unsigned(out, extracted, 16, false, 0);
          } else {
            detail::format_unsigned(out, extracted, 10, false, 0);
          }
        }
      }
    }

    if (first) {
      out.write("NONE");
    }

    out.put(']');
  }
};

// ============================================================================
// Single Field Descriptor Helpers
// ============================================================================

// 1-bit boolean flag at bit position 'pos'
#define MICROFMT_BIT_FLAG(pos, name)                                           \
  ::microfmt::bit_field {                                                      \
    (1u << (pos)), static_cast<uint8_t>(pos), (name),                          \
        ::microfmt::bit_type::flag                                             \
  }

// Multi-bit masked integer field (mask, shift, name)
#define MICROFMT_BIT_VALUE_DEC(mask, shift, name)                              \
  ::microfmt::bit_field {                                                      \
    (mask), static_cast<uint8_t>(shift), (name),                               \
        ::microfmt::bit_type::value_dec                                        \
  }

#define MICROFMT_BIT_VALUE_HEX(mask, shift, name)                              \
  ::microfmt::bit_field {                                                      \
    (mask), static_cast<uint8_t>(shift), (name),                               \
        ::microfmt::bit_type::value_hex                                        \
  }

// ============================================================================
// Register Type Synthesis Macro
// ============================================================================

#define MICROFMT_DEFINE_REGISTER_TYPE(TypeName, UnderlyingType, ...)           \
  struct TypeName {                                                            \
    UnderlyingType value{0};                                                   \
                                                                               \
    constexpr TypeName() noexcept = default;                                   \
    constexpr TypeName(UnderlyingType v) noexcept : value(v) {}                \
    constexpr explicit operator UnderlyingType() const noexcept {              \
      return value;                                                            \
    }                                                                          \
                                                                               \
    static constexpr ::microfmt::bit_field fields[] = {__VA_ARGS__};           \
  };                                                                           \
                                                                               \
  template <> struct microfmt::formatter<TypeName> {                           \
    constexpr void parse(::microfmt::format_parse_context &) noexcept {}       \
    void format(const TypeName &reg,                                           \
                const ::microfmt::sink &out) const noexcept {                  \
      ::microfmt::formatter<::microfmt::bitfield_view> f;                      \
      f.format(::microfmt::bits(static_cast<uint32_t>(reg.value),              \
                                ::microfmt::span(TypeName::fields)),           \
               out);                                                           \
    }                                                                          \
  };

} // namespace microfmt
