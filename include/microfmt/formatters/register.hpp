// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file register.hpp @brief Static bitfield / register descriptor formatting
 * views. */

#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace microfmt {

// ============================================================================
// Static Bitfield / Register Descriptors (Placed in Flash / .rodata)
// ============================================================================

/**
 * @brief Describes a single bitfield within a register.
 */
struct reg_field {
  /**
   * @brief Human-readable field name.
   */
  microfmt::string_view name;
  /**
   * @brief Bit offset of the field within the register word.
   */
  uint8_t bit_offset;
  /**
   * @brief Bit width of the field.
   */
  uint8_t bit_width;
  /**
   * @brief When `true` (single-bit flag), render as `+NAME`/`!NAME`.
   */
  bool is_flag{false}; // If true (width=1), format as +FLAG / -FLAG or flag name only
};

/**
 * @brief Static register descriptor listing its bitfields.
 *
 * Lives in flash / `.rodata`; the field array is sized at compile time.
 *
 * @tparam N Number of described fields.
 */
template <size_t N> struct reg_descriptor {
  /**
   * @brief Register name.
   */
  microfmt::string_view name;
  /**
   * @brief Register width in bytes (1, 2, 4, or 8).
   */
  size_t byte_width{4}; // 1, 2, 4, or 8 bytes
  /**
   * @brief The @p N field descriptors.
   */
  reg_field fields[N];
};

/**
 * @brief Deduction guide deriving the field count for @ref reg_descriptor.
 */
template <typename... Fields>
reg_descriptor(microfmt::string_view, size_t, Fields...) -> reg_descriptor<sizeof...(Fields)>;

// ============================================================================
// Register Value + Descriptor View
// ============================================================================

/**
 * @brief Non-owning view pairing a register value with its static descriptor.
 * @tparam N Number of described fields.
 * @tparam UInt Underlying unsigned register word type.
 */
template <size_t N, typename UInt = uint32_t> struct reg_view {
  /**
   * @brief Raw register value.
   */
  UInt value;
  /**
   * @brief Pointer to the static register descriptor.
   */
  const reg_descriptor<N> *desc{nullptr};
};

/**
 * @brief Builds a @ref reg_view from a register value and its descriptor.
 * @tparam N Number of described fields.
 * @tparam UInt Underlying unsigned register word type.
 * @param val Raw register value.
 * @param desc Static descriptor naming the register.
 * @return A formattable @ref reg_view.
 */
template <size_t N, typename UInt>
[[nodiscard]] constexpr auto format_reg(UInt val, const reg_descriptor<N> &desc) noexcept {
  return reg_view<N, UInt>{val, &desc};
}

// ============================================================================
// Formatter for Register View
// ============================================================================

/**
 * @brief Formatter for @ref reg_view that decodes and renders register fields.
 *
 * Supports the `s`/`S` flag for a short mode (only active fields) and the
 * `n`/`N` flag to omit the outer `[...]` brackets.
 *
 * @tparam N Number of described fields.
 * @tparam UInt Underlying unsigned register word type.
 */
template <size_t N, typename UInt> struct formatter<reg_view<N, UInt>> {
  /**
   * @brief `d` = detailed (default), `s` = short (active fields only).
   */
  bool detailed{true}; // 'd' -> detailed fields, 's' -> short (active flags only)
  /**
   * @brief Set to `true` (via `n`/`N`) to omit the outer braces.
   */
  bool naked{false}; // 'n' -> no outer braces

  /**
   * @brief Parses the `s`/`S` and `n`/`N` mode flags.
   * @param ctx Format parse context exposing the specifier text.
   */
  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    for (char c : spec) {
      if (c == 's' || c == 'S')
        detailed = false;
      else if (c == 'n' || c == 'N')
        naked = true;
    }
  }

  /**
   * @brief Renders the register name, raw value, and decoded bitfields.
   * @param rv The register view to format.
   * @param out Destination sink.
   */
  void format(const reg_view<N, UInt> &rv, const sink &out) const noexcept {
    if (!rv.desc)
      return;
    const auto &d = *rv.desc;

    // Register Name & Raw Value
    out.write(d.name);
    out.write("=0x");
    detail::format_unsigned(out, static_cast<uint64_t>(rv.value), 16, true, static_cast<int>(d.byte_width * 2));

    // Decode Bitfields
    if (!naked)
      out.write(" [");
    else
      out.put(' ');

    size_t printed_count = 0;
    for (size_t i = 0; i < N; ++i) {
      RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

      const auto &f = d.fields[i];

      RELOCO_END_UNSAFE_BUFFER_USAGE

      const uint64_t mask = (f.bit_width == 64) ? ~0ULL : ((1ULL << f.bit_width) - 1ULL);
      const uint64_t val = (static_cast<uint64_t>(rv.value) >> f.bit_offset) & mask;

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