// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdio>

#include <microfmt/microfmt.hpp>
#include <microfmt/parsing/lexer_formatters.hpp>
#include <microfmt/parsing/lexer_rules_presets.hpp>

int main() {
  constexpr microfmt::string_view source = "if (x >= 0x2A) { return \"hi\"; } // done";

  // Tokenize with the C-like preset and print one token per line.
  microfmt::buffer_sink<1024> out;
  microfmt::lexer lx{source, microfmt::rules::c_like};
  for (microfmt::token tok = lx.next(); tok.kind != microfmt::token_kind::end; tok = lx.next()) {
    microfmt::format_to(out.as_sink(), "{}\n", tok);
  }

  // Colour the same text with ANSI escape codes (whitespace and comments kept).
  microfmt::format_to(out.as_sink(), "{}\n", microfmt::highlight(source, microfmt::rules::c_like));

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  std::printf("%.*s", static_cast<int>(out.view().size()), out.view().data());

  RELOCO_END_UNSAFE_BUFFER_USAGE;
}
