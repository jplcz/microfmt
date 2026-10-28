// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

// std-interop-file: the std::string&& overload of highlight() is deleted to reject dangling temporaries.

/** @file lexer_formatters.hpp
 * @brief Formatters for `token_kind`/`token` and an ANSI syntax `highlight()` view.
 *
 * Why: lets you log tokens while debugging a grammar and colour source text
 * on a terminal. Kept apart from `lexer.hpp` so the bare engine does not pull
 * in formatter or ANSI code.
 */

#include "../formatters/ansi.hpp"
#include "../formatters/escaped.hpp"
#include "lexer.hpp"
#include <string>
#include <type_traits>

namespace microfmt {

/** Lowercase name of a token kind (`identifier`, `op`, ...). */
[[nodiscard]] constexpr microfmt::string_view token_kind_name(token_kind kind) noexcept {
  switch (kind) {
  case token_kind::end:
    return "end";
  case token_kind::error:
    return "error";
  case token_kind::whitespace:
    return "whitespace";
  case token_kind::newline:
    return "newline";
  case token_kind::identifier:
    return "identifier";
  case token_kind::keyword:
    return "keyword";
  case token_kind::integer:
    return "integer";
  case token_kind::floating:
    return "floating";
  case token_kind::string:
    return "string";
  case token_kind::comment:
    return "comment";
  case token_kind::op:
    return "op";
  case token_kind::punctuation:
    return "punctuation";
  }
  return "unknown";
}

/** `{}` prints the kind name; padding specifiers are not supported, wrap with `pad()` for alignment. */
template <> struct formatter<token_kind> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(token_kind kind, const sink &out) const noexcept { out.write(token_kind_name(kind)); }
};

/** `{}` prints `kind "escaped text" @line:column`, safe to log even for control bytes. */
template <> struct formatter<token> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const token &tok, const sink &out) const noexcept {
    out.write(token_kind_name(tok.kind));
    out.put(' ');
    formatter<escaped_view>{}.format(escaped(tok.text), out);
    out.write(" @");
    formatter<uint32_t>{}.format(tok.line, out);
    out.put(':');
    formatter<uint32_t>{}.format(tok.column, out);
  }
};

/** Colour per token class; a default-constructed style prints the text unstyled. */
struct highlight_theme {
  ansi::style keyword{ansi::color::bright_magenta, ansi::color::none, ansi::attribute::bold};
  ansi::style identifier{};
  ansi::style number{ansi::color::bright_yellow};
  ansi::style string{ansi::color::bright_green};
  ansi::style comment{ansi::color::bright_black, ansi::color::none, ansi::attribute::italic};
  ansi::style op{ansi::color::bright_cyan};
  ansi::style punctuation{};
  ansi::style error{ansi::color::bright_red, ansi::color::none, ansi::attribute::underline};

  [[nodiscard]] constexpr ansi::style style_for(token_kind kind) const noexcept {
    switch (kind) {
    case token_kind::keyword:
      return keyword;
    case token_kind::identifier:
      return identifier;
    case token_kind::integer:
    case token_kind::floating:
      return number;
    case token_kind::string:
      return string;
    case token_kind::comment:
      return comment;
    case token_kind::op:
      return op;
    case token_kind::punctuation:
      return punctuation;
    case token_kind::error:
      return error;
    case token_kind::end:
    case token_kind::whitespace:
    case token_kind::newline:
      break;
    }
    return ansi::style{};
  }
};

/** Borrowing view that renders `text` with ANSI colours; output minus escape codes equals the input. */
struct RELOCO_POINTER highlight_view {
  microfmt::string_view text{};
  lexer_rules rules{};
  highlight_theme theme{};
};

/** @param text Source to colour; must outlive the view. @param rules Language rules. @param theme Colours. */
[[nodiscard]] constexpr highlight_view highlight(microfmt::string_view text RELOCO_LIFETIMEBOUND,
                                                 const lexer_rules &rules, const highlight_theme &theme = {}) noexcept {
  return highlight_view{text, rules, theme};
}
template <typename S, std::enable_if_t<std::is_same_v<S, std::string>, int> = 0>
highlight_view highlight(S &&, const lexer_rules &, const highlight_theme & = {}) = delete;

template <> struct formatter<highlight_view> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const highlight_view &view, const sink &out) const noexcept {
    // Keep whitespace and comments so the text is reproduced byte for byte.
    lexer_rules rules = view.rules;
    rules.skip_whitespace = false;
    rules.skip_comments = false;

    lexer lx{view.text, rules};
    for (token tok = lx.next(); tok.kind != token_kind::end; tok = lx.next()) {
      const ansi::style st = view.theme.style_for(tok.kind);
      const bool plain = !ansi::style::colors_enabled || (st.fg == ansi::color::none && st.bg == ansi::color::none &&
                                                          st.attr == ansi::attribute::none);
      if (plain) {
        out.write(tok.text);
      } else {
        ansi::emit_style(st, out);
        out.write(tok.text);
        ansi::emit_reset(out);
      }
    }
  }
};

} // namespace microfmt
