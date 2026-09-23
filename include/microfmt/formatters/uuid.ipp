// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file uuid.ipp @brief Out-of-line body for detail::format_uuid_bytes
 * (see uuid.hpp). Included from uuid.hpp itself (inside namespace
 * microfmt::detail), guarded on MICROFMT_SHARED_PROVIDE_DEFINITIONS (see
 * microfmt/detail/compat.hpp). Never included directly. */

MICROFMT_API void format_uuid_bytes(const sink &out, span<const uint8_t> bytes, bool is_upper,
                                     bool is_braced) noexcept {
  if (bytes.size() < 16) {
    out.write("00000000-0000-0000-0000-000000000000");
    return;
  }

  const auto &hex_digits = is_upper ? detail::hex_digits_upper : detail::hex_digits_lower;

  if (is_braced) {
    out.put('{');
  }

  // 8-4-4-4-12 canonical layout
  for (size_t i = 0; i < 16; ++i) {
    const uint8_t b = bytes[i];
    out.put(hex_digits[(b >> 4) & 0x0F]);
    out.put(hex_digits[b & 0x0F]);

    if (i == 3 || i == 5 || i == 7 || i == 9) {
      out.put('-');
    }
  }

  if (is_braced) {
    out.put('}');
  }
}
