// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file bitfield.ipp @brief Out-of-line body for formatter<bitfield_view>::format
 * (see bitfield.hpp). Included from bitfield.hpp itself, guarded on
 * MICROFMT_SHARED_PROVIDE_DEFINITIONS (see microfmt/detail/compat.hpp).
 * Never included directly. */

MICROFMT_API void formatter<bitfield_view>::format(const bitfield_view &bv, const sink &out) const noexcept {
  if (bv.show_raw_hex) {
    out.write("0x");
    detail::format_unsigned<detail::radix::hex>(out, bv.raw_value, false, 8);
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
          detail::format_unsigned<detail::radix::hex>(out, extracted, false, 0);
        } else {
          detail::format_unsigned<detail::radix::decimal>(out, extracted, false, 0);
        }
      }
    }
  }

  if (first) {
    out.write("NONE");
  }

  out.put(']');
}
