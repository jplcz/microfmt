#pragma once

#include "microfmt.hpp"
#include <cstdint>
#include <type_traits>

namespace microfmt {

namespace detail {

// Compile-time power of 10 for uint64_t
constexpr uint64_t ct_pow10(uint8_t exp) noexcept {
  uint64_t result = 1;
  for (uint8_t i = 0; i < exp; ++i) {
    result *= 10;
  }
  return result;
}

} // namespace detail

// ============================================================================
// Fixed-Point View Adapter
// ============================================================================
// Scale: Value representation divider (e.g., Scale = 1000 for
// millivolts/milliseconds) Decimals: Number of fractional digits to format (0
// to 18)
template <uint64_t Scale, uint8_t Decimals = 2, typename IntType = int32_t>
struct fixed_point_view {
  static_assert(Scale > 0, "fixed_point Scale must be greater than 0");
  static_assert(Decimals <= 18,
                "fixed_point Decimals exceeds 64-bit precision");
  static_assert(std::is_integral_v<IntType>,
                "fixed_point value must be an integral type");

  IntType raw_value{0};
};

// Convenience factory function
template <uint64_t Scale, uint8_t Decimals = 2, typename IntType>
[[nodiscard]] constexpr auto fixed(IntType raw_val) noexcept {
  return fixed_point_view<Scale, Decimals, IntType>{raw_val};
}

// Common embedded aliases
template <typename IntType = int32_t>
using milli_view = fixed_point_view<1000, 3, IntType>; // e.g., mV -> V, ms -> s

template <typename IntType = int32_t>
using centi_view = fixed_point_view<100, 2, IntType>; // e.g., 0.01 increments

template <typename IntType = int32_t>
using micro_view =
    fixed_point_view<1000000, 6, IntType>; // e.g., us -> s, uA -> A

template <typename IntType>
[[nodiscard]] constexpr auto milli(IntType raw_val) noexcept {
  return milli_view<IntType>{raw_val};
}

template <typename IntType>
[[nodiscard]] constexpr auto centi(IntType raw_val) noexcept {
  return centi_view<IntType>{raw_val};
}

template <typename IntType>
[[nodiscard]] constexpr auto micro(IntType raw_val) noexcept {
  return micro_view<IntType>{raw_val};
}

// ============================================================================
// Formatter Specialization for fixed_point_view
// ============================================================================
template <uint64_t Scale, uint8_t Decimals, typename IntType>
struct formatter<fixed_point_view<Scale, Decimals, IntType>> {
  constexpr void parse(format_parse_context &ctx) noexcept { (void)ctx; }

  void format(const fixed_point_view<Scale, Decimals, IntType> &fp,
              const sink &out) const noexcept {
    using UnsignedType =
        std::make_unsigned_t<std::conditional_t<(sizeof(IntType) < 4), uint32_t,
                                                std::make_unsigned_t<IntType>>>;

    bool is_negative = false;
    UnsignedType abs_raw = 0;

    if constexpr (std::is_signed_v<IntType>) {
      if (fp.raw_value < 0) {
        is_negative = true;
        // Avoid undefined behavior on INT_MIN
        abs_raw = static_cast<UnsignedType>(0) -
                  static_cast<UnsignedType>(fp.raw_value);
      } else {
        abs_raw = static_cast<UnsignedType>(fp.raw_value);
      }
    } else {
      abs_raw = fp.raw_value;
    }

    const uint64_t integer_part = static_cast<uint64_t>(abs_raw / Scale);
    const uint64_t remainder = static_cast<uint64_t>(abs_raw % Scale);

    // Compute fractional representation scaled to the requested number of
    // decimals
    constexpr uint64_t target_mult = detail::ct_pow10(Decimals);
    uint64_t frac_part = 0;

    if constexpr (Decimals > 0) {
      // remainder * target_mult / Scale
      frac_part = (remainder * target_mult) / Scale;
    }

    // Output Sign (handles -0.05 correctly where integer_part is 0)
    if (is_negative && (integer_part > 0 || frac_part > 0)) {
      out.put('-');
    }

    // Output Integer Part
    detail::format_unsigned(out, integer_part, 10, false, 0);

    // Output Fractional Part (zero-padded to Decimals width)
    if constexpr (Decimals > 0) {
      out.put('.');
      detail::format_unsigned(out, frac_part, 10, false, Decimals);
    }
  }
};

} // namespace microfmt