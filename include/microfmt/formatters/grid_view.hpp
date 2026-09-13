#pragma once

#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace microfmt {

// ============================================================================
// Generic Named Register / Field Descriptor
// ============================================================================

template <typename WordType, size_t N> struct reg_grid_desc {
  std::string_view title;
  uint8_t columns{4}; // Number of register entries per row
  std::string_view names[N];
};

// Deduction guide
template <typename WordType, typename... Names>
reg_grid_desc(std::string_view, uint8_t, Names...)
    -> reg_grid_desc<WordType, sizeof...(Names)>;

// ============================================================================
// Grid View Wrapper
// ============================================================================

template <typename WordType, size_t N> struct reg_grid_view {
  const WordType *values{nullptr};
  const reg_grid_desc<WordType, N> *desc{nullptr};
};

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

template <typename WordType, size_t N>
struct formatter<reg_grid_view<WordType, N>> {
  static constexpr size_t hex_digits = sizeof(WordType) * 2;

  constexpr void parse(format_parse_context &) noexcept {}

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
      std::string_view name = d.names[i];
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