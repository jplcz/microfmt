// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file escaped.ipp @brief Out-of-line body for formatter<escaped_view>::format
 * (see escaped.hpp). Included from escaped.hpp itself, guarded on
 * MICROFMT_SHARED_PROVIDE_DEFINITIONS (see microfmt/detail/compat.hpp).
 * Never included directly. */

MICROFMT_API void formatter<escaped_view>::format(const escaped_view &ev, const sink &out) const noexcept {
  if (ev.quote) {
    out.put('"');
  }

  for (const char ch : ev.data) {
    detail::write_escaped_char(out, ch, '"', ev.escape_quotes);
  }

  if (ev.quote) {
    out.put('"');
  }
}
