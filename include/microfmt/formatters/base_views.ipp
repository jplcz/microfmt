// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

namespace microfmt {

MICROFMT_API void formatter<base64_view, void>::format(const base64_view &bv, const sink &out) const noexcept {
  static constexpr char B64_TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  const size_t len = bv.data.size();
  size_t i = 0;

  while (i + 2 < len) {
    const uint32_t triple = (static_cast<uint32_t>(bv.data[i]) << 16) | (static_cast<uint32_t>(bv.data[i + 1]) << 8) |
                            static_cast<uint32_t>(bv.data[i + 2]);
    out.put(B64_TABLE[(triple >> 18) & 0x3F]);
    out.put(B64_TABLE[(triple >> 12) & 0x3F]);
    out.put(B64_TABLE[(triple >> 6) & 0x3F]);
    out.put(B64_TABLE[triple & 0x3F]);
    i += 3;
  }

  if (i < len) {
    uint32_t triple = static_cast<uint32_t>(bv.data[i]) << 16;
    if (i + 1 < len) {
      triple |= static_cast<uint32_t>(bv.data[i + 1]) << 8;
      out.put(B64_TABLE[(triple >> 18) & 0x3F]);
      out.put(B64_TABLE[(triple >> 12) & 0x3F]);
      out.put(B64_TABLE[(triple >> 6) & 0x3F]);
      out.put('=');
    } else {
      out.put(B64_TABLE[(triple >> 18) & 0x3F]);
      out.put(B64_TABLE[(triple >> 12) & 0x3F]);
      out.put('=');
      out.put('=');
    }
  }
}


}