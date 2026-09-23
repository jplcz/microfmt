// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

namespace microfmt::ansi {
MICROFMT_API void emit_style(const style &s, const sink &out) noexcept {
  if (!style::colors_enabled) {
    return;
  }

  if (s.fg == color::none && s.bg == color::none && s.attr == attribute::none) {
    out.write("\033[0m");
    return;
  }

  out.write("\033[");
  bool first = true;

  auto emit_code = [&](uint8_t code) noexcept {
    if (!first)
      out.put(';');
    first = false;
    detail::format_unsigned<detail::radix::decimal>(out, code, false, 0);
  };

  // Attributes
  if (s.attr & attribute::bold)
    emit_code(1);
  if (s.attr & attribute::dim)
    emit_code(2);
  if (s.attr & attribute::italic)
    emit_code(3);
  if (s.attr & attribute::underline)
    emit_code(4);
  if (s.attr & attribute::blink)
    emit_code(5);
  if (s.attr & attribute::reverse)
    emit_code(7);

  // Foreground color
  if (s.fg != color::none) {
    emit_code(static_cast<uint8_t>(s.fg));
  }

  // Background color (standard +10, bright +10)
  if (s.bg != color::none) {
    emit_code(static_cast<uint8_t>(s.bg) + 10);
  }

  out.put('m');
}

MICROFMT_API void emit_reset(const sink &out) noexcept {
  if (style::colors_enabled) {
    out.write("\033[0m");
  }
}
} // namespace microfmt::ansi
