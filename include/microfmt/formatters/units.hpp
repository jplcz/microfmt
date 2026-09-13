#pragma once

#include "../microfmt.hpp"
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace microfmt {

// ============================================================================
// Unit Scaling Modes
// ============================================================================

enum class scale_base : uint16_t {
  decimal = 1000, // SI: Hz, V, A, s, m (k, M, G, T)
  binary = 1024   // IEC: Bytes (KiB, MiB, GiB, TiB)
};

// ============================================================================
// Unit View Descriptors
// ============================================================================

// Auto-scaled View (e.g., 1048576 B -> "1.00 MiB" or 50000000 Hz -> "50.00
// MHz")
template <typename T> struct auto_unit_view {
  T value{0};
  std::string_view unit_symbol{};
  scale_base base{scale_base::decimal};
  uint8_t precision{2}; // Decimal places
};

// Explicit Fixed Unit View (e.g., 42 "mA" -> "42 mA")
template <typename T> struct unit_view {
  T value{0};
  std::string_view unit_symbol{};
};

// ============================================================================
// Factory Helpers
// ============================================================================

// Auto-scaling SI decimal units (Hz, W, V, etc.)
template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
[[nodiscard]] constexpr auto auto_si(T val, std::string_view unit,
                                     uint8_t precision = 2) noexcept {
  return auto_unit_view<T>{val, unit, scale_base::decimal, precision};
}

// Auto-scaling IEC binary units (B / Bytes)
template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
[[nodiscard]] constexpr auto auto_bytes(T bytes,
                                        uint8_t precision = 2) noexcept {
  return auto_unit_view<T>{bytes, "B", scale_base::binary, precision};
}

// Fixed unit tag (no scaling)
template <typename T>
[[nodiscard]] constexpr auto with_unit(T val, std::string_view unit) noexcept {
  return unit_view<T>{val, unit};
}

// Frequency convenience helper
template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
[[nodiscard]] constexpr auto hertz(T hz, uint8_t precision = 2) noexcept {
  return auto_unit_view<T>{hz, "Hz", scale_base::decimal, precision};
}

// ============================================================================
// Formatter Specializations
// ============================================================================

// --- Fixed Unit Formatter ---
template <typename T> struct formatter<unit_view<T>> {
  std::string_view forwarded_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept {
    forwarded_spec = ctx.spec();
  }

  void format(const unit_view<T> &uv, const sink &out) const noexcept {
    formatter<T> val_fmt;
    format_parse_context val_ctx(forwarded_spec);
    val_fmt.parse(val_ctx);
    val_fmt.format(uv.value, out);

    out.put(' ');
    out.write(uv.unit_symbol);
  }
};

// --- Auto-Scaling Formatter (Zero-Float Fixed-Point Scaling) ---
template <typename T> struct formatter<auto_unit_view<T>> {
  uint8_t parse_precision{255}; // 255 = not set via format string

  constexpr void parse(format_parse_context &ctx) noexcept {
    // Parse precision from format string, e.g. "{:.1}" or "{:.3}"
    auto spec = ctx.spec();
    for (size_t i = 0; i < spec.size(); ++i) {
      if (spec[i] == '.' && (i + 1) < spec.size()) {
        char next = spec[i + 1];
        if (next >= '0' && next <= '9') {
          parse_precision = static_cast<uint8_t>(next - '0');
        }
      }
    }
  }

  void format(const auto_unit_view<T> &auv, const sink &out) const noexcept {
    static constexpr const char *const SI_PREFIXES[] = {"",  "k", "M",
                                                        "G", "T", "P"};
    static constexpr const char *const IEC_PREFIXES[] = {"",   "Ki", "Mi",
                                                         "Gi", "Ti", "Pi"};

    const uint32_t base_div = static_cast<uint32_t>(auv.base);
    const char *const *prefixes =
        (auv.base == scale_base::binary) ? IEC_PREFIXES : SI_PREFIXES;

    const bool is_negative = auv.value < 0;
    uint64_t abs_val = is_negative ? static_cast<uint64_t>(-auv.value)
                                   : static_cast<uint64_t>(auv.value);

    size_t scale_idx = 0;
    uint64_t remainder = 0;
    uint64_t current_div = 1;

    // Scale up until value fits under the base divisor
    while (abs_val >= base_div && scale_idx < 5) {
      remainder = abs_val % base_div;
      abs_val /= base_div;
      current_div = base_div;
      scale_idx++;
    }

    if (is_negative) {
      out.put('-');
    }

    // Whole part
    detail::format_unsigned(out, abs_val, 10, false, 0);

    const uint8_t prec =
        (parse_precision != 255) ? parse_precision : auv.precision;

    // Fractional part via pure integer arithmetic
    if (prec > 0 && scale_idx > 0) {
      out.put('.');
      uint64_t frac_multiplier = 1;
      for (uint8_t i = 0; i < prec; ++i)
        frac_multiplier *= 10;

      uint64_t frac_part = (remainder * frac_multiplier) / current_div;
      detail::format_unsigned(out, frac_part, 10, false, prec);
    }

    out.put(' ');
    out.write(prefixes[scale_idx]);
    out.write(auv.unit_symbol);
  }
};

} // namespace microfmt