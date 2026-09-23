// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file pointer.ipp @brief Out-of-line bodies for formatter<raw_ptr_view>
 * (see pointer.hpp). Included from pointer.hpp itself, guarded on
 * MICROFMT_SHARED_PROVIDE_DEFINITIONS (see microfmt/detail/compat.hpp).
 * Never included directly. */

MICROFMT_API_CONSTEXPR void formatter<raw_ptr_view>::parse(format_parse_context &ctx) noexcept {
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
    } else if (c == '0' && (i + 1) < spec.size() && spec[i + 1] >= '0' && spec[i + 1] <= '9') {
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

MICROFMT_API void formatter<raw_ptr_view>::format(const raw_ptr_view &ptr, const sink &out) const noexcept {
  if (ptr.is_null) {
    out.write(null_representation);
    return;
  }

  if (show_prefix) {
    out.write(uppercase_hex ? "0X" : "0x");
  }

  // Determine target width
  size_t width = native_hex_width;
  const ptr_width_mode effective_mode = (mode != ptr_width_mode::native) ? mode : ptr.width_mode;

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

  const int output_width = width > static_cast<size_t>(std::numeric_limits<int>::max())
                               ? std::numeric_limits<int>::max()
                               : static_cast<int>(width);
  detail::format_unsigned<detail::radix::hex>(out, addr, uppercase_hex, output_width);
}
