// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file lexer_rules_presets.hpp
 * @brief Ready-made `lexer_rules` for C-like languages, JSON and POSIX shell.
 *
 * Why: they are useful out of the box and double as worked examples of how
 * to describe a language; copy and tweak them for your own syntax.
 */

#include "lexer.hpp"

namespace microfmt::rules {

namespace detail {

inline constexpr keyword_table c_like_keywords{"break", "case",   "const",  "continue", "default", "do",     "else",
                                               "enum",  "for",    "if",     "return",   "sizeof",  "static", "struct",
                                               "switch", "typedef", "union", "while"};
inline constexpr microfmt::string_view c_like_operators[] = {
    "<<=", ">>=", "...", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "+=", "-=", "*=",
    "/=",  "%=",  "&=",  "|=", "^=", "+",  "-",  "*",  "/",  "%",  "&",  "|",  "^",  "~",  "!",  "<",  ">",
    "=",   "?",   ":",   "."};

inline constexpr keyword_table json_keywords{"false", "null", "true"};

inline constexpr keyword_table shell_keywords{"case", "do", "done", "elif", "else", "esac", "fi", "for", "if", "in", "then", "while"};
inline constexpr microfmt::string_view shell_operators[] = {"&&", "||", ";;", ">>", "<<", "|", "&", ">", "<", "="};

[[nodiscard]] constexpr lexer_rules make_c_like() noexcept {
  lexer_rules r;
  r.keywords = c_like_keywords;
  r.operators = c_like_operators;
  r.punctuation = "(){}[],;";
  r.line_comment = "//";
  r.block_comment_open = "/*";
  r.block_comment_close = "*/";
  return r;
}

[[nodiscard]] constexpr lexer_rules make_json() noexcept {
  lexer_rules r;
  r.keywords = json_keywords;
  r.punctuation = "{}[],:";
  r.quotes = "\"";
  r.digit_separator = '\0';
  r.hex_binary_prefixes = false;
  return r;
}

[[nodiscard]] constexpr lexer_rules make_shell() noexcept {
  lexer_rules r;
  r.keywords = shell_keywords;
  r.operators = shell_operators;
  r.punctuation = "(){}[];";
  r.line_comment = "#";
  r.identifier_start_extra = "$-";
  r.identifier_continue_extra = "-.";
  r.digit_separator = '\0';
  r.hex_binary_prefixes = false;
  return r;
}

} // namespace detail

/** C-like language: `//` and non-nested block comments, quotes `"` and `'`, 0x/0b literals. */
inline constexpr lexer_rules c_like = detail::make_c_like();

/** JSON: only `"` strings, no comments or operators, keywords `true`/`false`/`null`. */
inline constexpr lexer_rules json = detail::make_json();

/** POSIX shell: `#` comments, `-flag`/`$var` words, redirection and logic operators. */
inline constexpr lexer_rules shell = detail::make_shell();

} // namespace microfmt::rules
