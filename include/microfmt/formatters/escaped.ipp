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
    const uint8_t b = static_cast<uint8_t>(ch);
    switch (b) {
    case '\0':
      out.write("\\0");
      break;
    case '\a':
      out.write("\\a");
      break;
    case '\b':
      out.write("\\b");
      break;
    case '\t':
      out.write("\\t");
      break;
    case '\n':
      out.write("\\n");
      break;
    case '\v':
      out.write("\\v");
      break;
    case '\f':
      out.write("\\f");
      break;
    case '\r':
      out.write("\\r");
      break;
    case '\\':
      out.write("\\\\");
      break;
    case '"':
      if (ev.escape_quotes) {
        out.write("\\\"");
      } else {
        out.put('"');
      }
      break;
    default:
      if (b >= 32 && b <= 126) {
        // Printable ASCII
        out.put(static_cast<char>(b));
      } else {
        // Non-printable byte -> \xHH
        out.write("\\x");
        detail::format_unsigned<detail::radix::hex>(out, b, false, 2);
      }
      break;
    }
  }

  if (ev.quote) {
    out.put('"');
  }
}
