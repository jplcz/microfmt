// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file grid_view.hpp @brief Register grid and field descriptor formatting
 * views. */

#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace microfmt {

// ============================================================================
// Generic Named Register / Field Descriptor
// ============================================================================

/**
 * @brief Static descriptor naming @p N register entries within a grid.
 *
 * Intended to live in flash / `.rodata` and pair with a contiguous array of
 * register words for rendering.
 *
 * @tparam WordType Underlying register word type.
 * @tparam N Number of register entries described.
 */
template <typename WordType, size_t N> struct reg_grid_desc {
  /**
   * @brief Optional grid title rendered as `=== title ===`.
   */
  microfmt::string_view title;
  /**
   * @brief Number of register entries rendered per row (defaults to `4`).
   */
  uint8_t columns{4}; // Number of register entries per row
  /**
   * @brief Per-entry names, one per register word.
   */
  microfmt::string_view names[N];
};

/**
 * @brief Empty tag used to drive CTAD for @ref reg_grid_desc.
 * @tparam T Underlying register word type.
 */
template <typename T> struct type_tag {};

/**
 * @brief Deduction guide deriving the entry count for @ref reg_grid_desc.
 */
template <typename WordType, typename... Names>
reg_grid_desc(type_tag<WordType>, microfmt::string_view, uint8_t, Names...)
    -> reg_grid_desc<WordType, sizeof...(Names)>;

// ============================================================================
// Grid View Wrapper
// ============================================================================

/**
 * @brief Non-owning view pairing register values with their static descriptor.
 * @tparam WordType Underlying register word type.
 * @tparam N Number of register entries.
 */
template <typename WordType, size_t N> struct reg_grid_view {
  /**
   * @brief Pointer to the contiguous array of register words.
   */
  const WordType *values{nullptr};
  /**
   * @brief Pointer to the static grid descriptor.
   */
  const reg_grid_desc<WordType, N> *desc{nullptr};
};

/**
 * @brief Builds a @ref reg_grid_view over an array/struct of register data.
 *
 * @tparam StructOrArray Source object type (must be at least `N *
 * sizeof(WordType)` bytes).
 * @tparam WordType Underlying register word type.
 * @tparam N Number of register entries in @p desc.
 * @param data Raw register storage (array or POD struct).
 * @param desc Static descriptor naming the entries.
 * @return A formattable @ref reg_grid_view.
 */
template <typename StructOrArray, typename WordType, size_t N>
[[nodiscard]] constexpr auto
make_reg_grid(const StructOrArray &data,
              const reg_grid_desc<WordType, N> &desc) noexcept {
  static_assert(
      sizeof(StructOrArray) >= N * sizeof(WordType),
      "Source structure size is smaller than register grid definition");
  return reg_grid_view<WordType, N>{reinterpret_cast<const WordType *>(&data),
                                    &desc};
}

// ============================================================================
// Formatter for Generic Register Grid
// ============================================================================

/**
 * @brief Formatter rendering @ref reg_grid_view as an aligned register grid.
 *
 * Emits an optional title, then per-entry `name = 0xXXXX` cells laid out with
 * @ref reg_grid_desc::columns entries per row.
 *
 * @tparam WordType Underlying register word type.
 * @tparam N Number of register entries.
 */
template <typename WordType, size_t N>
struct formatter<reg_grid_view<WordType, N>> {
  /**
   * @brief Number of hex digits used per word (zero-padded).
   */
  static constexpr size_t hex_digits = sizeof(WordType) * 2;

  /**
   * @brief No-op parse; grid views accept no format specifier.
   */
  constexpr void parse(format_parse_context &) noexcept {}

  /**
   * @brief Renders the register grid to the output sink.
   * @param gv The grid view to format.
   * @param out Destination sink.
   */
  void format(const reg_grid_view<WordType, N> &gv,
              const sink &out) const noexcept {
    if (!gv.desc || !gv.values)
      return;
    const auto &d = *gv.desc;

    if (!d.title.empty()) {
      out.write("=== ");
      out.write(d.title);
      out.write(" ===\n");
    }

    const uint8_t cols = (d.columns > 0) ? d.columns : 4;

    for (size_t i = 0; i < N; ++i) {
      microfmt::string_view name = d.names[i];
      out.write(name);

      // Pad name column to 5 characters for clean alignment
      if (name.size() < 5) {
        for (size_t p = name.size(); p < 5; ++p)
          out.put(' ');
      }
      out.write("= 0x");

      detail::format_unsigned(out, static_cast<uint64_t>(gv.values[i]), 16,
                              true, hex_digits);

      // Newline or column separator
      if ((i + 1) % cols == 0 || (i + 1) == N) {
        out.put('\n');
      } else {
        out.write("  ");
      }
    }
  }
};

} // namespace microfmt