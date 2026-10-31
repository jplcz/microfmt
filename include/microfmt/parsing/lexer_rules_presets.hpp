// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file lexer_rules_presets.hpp
 * @brief Ready-made `lexer_rules` for C, C++, Lua, SQL, INI, TOML, config files, JSON and POSIX shell.
 *
 * Why: they are useful out of the box and double as worked examples of how
 * to describe a language; copy and tweak them for your own syntax.
 */

#include "lexer.hpp"

namespace microfmt::rules {

namespace detail {

inline constexpr keyword_table c_keywords{
    "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic", "_Noreturn", "_Static_assert", "_Thread_local",
    "alignas", "alignof", "auto", "bool", "break", "case", "char", "const", "constexpr", "continue", "default", "do",
    "double", "else", "enum", "extern", "false", "float", "for", "goto", "if", "inline", "int", "long", "nullptr",
    "register", "restrict", "return", "short", "signed", "sizeof", "static", "static_assert", "struct", "switch",
    "thread_local", "true", "typedef", "typeof", "union", "unsigned", "void", "volatile", "while"};

// std-interop-begin: the words catch/throw below are C++ keyword spellings, not exception use
inline constexpr keyword_table cpp_keywords{
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break", "case", "catch", "char",
    "char16_t", "char32_t", "char8_t", "class", "co_await", "co_return", "co_yield", "compl", "concept", "const",
    "const_cast", "consteval", "constexpr", "constinit", "continue", "decltype", "default", "delete", "do", "double",
    "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "float", "for", "friend", "goto", "if",
    "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or",
    "or_eq", "private", "protected", "public", "register", "reinterpret_cast", "requires", "return", "short", "signed",
    "sizeof", "static", "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local",
    "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual", "void",
    "volatile", "wchar_t", "while", "xor", "xor_eq"};
// std-interop-end

// NOLINTNEXTLINE(reloco-no-c-array): constant table exposed to lexer_rules as a span
inline constexpr microfmt::string_view c_operators[] = {
    "<<=", ">>=", "...", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "+=", "-=", "*=", "/=",
    "%=",  "&=",  "|=",  "^=", "+",  "-",  "*",  "/",  "%",  "&",  "|",  "^",  "~",  "!",  "<",  ">",  "=",  "?",
    ":",   "."};

// NOLINTNEXTLINE(reloco-no-c-array): constant table exposed to lexer_rules as a span
inline constexpr microfmt::string_view cpp_operators[] = {
    "<=>", "->*", "<<=", ">>=", "...", "::", ".*", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
    "+=",  "-=",  "*=",  "/=",  "%=",  "&=", "|=", "^=", "+",  "-",  "*",  "/",  "%",  "&",  "|",  "^",  "~",  "!",
    "<",   ">",   "=",   "?",   ":",   "."};

// NOLINTNEXTLINE(reloco-no-c-array): constant table exposed to lexer_rules as a span
inline constexpr microfmt::string_view c_string_prefixes[] = {"u8", "u", "U", "L"};
// NOLINTNEXTLINE(reloco-no-c-array): constant table exposed to lexer_rules as a span
inline constexpr microfmt::string_view cpp_string_prefixes[] = {"u8R", "uR", "UR", "LR", "R", "u8", "u", "U", "L"};
// NOLINTNEXTLINE(reloco-no-c-array): constant table exposed to lexer_rules as a span
inline constexpr microfmt::string_view c_number_suffixes[] = {"u", "l", "ul", "lu", "ll", "ull", "llu", "f"};

inline constexpr keyword_table lua_keywords{"and",   "break", "do",    "else", "elseif", "end",    "false",
                                            "for",   "function", "goto", "if",   "in",     "local",  "nil",
                                            "not",   "or",    "repeat", "return", "then", "true",   "until",
                                            "while"};
// NOLINTNEXTLINE(reloco-no-c-array): constant table exposed to lexer_rules as a span
inline constexpr microfmt::string_view lua_operators[] = {"...", "..", "==", "~=", "<=", ">=", "//", "::", "<<", ">>",
                                                          "+",   "-",  "*",  "/",  "%",  "^",  "#",  "&",  "~",  "|",
                                                          "<",   ">",  "=",  ".",  ":"};

// Lower-case on purpose: the SQL preset sets keywords_ignore_case.
inline constexpr keyword_table sql_keywords{
    "add", "all", "alter", "and", "as", "asc", "begin", "between", "by", "case", "check", "column", "commit",
    "constraint", "create", "cross", "database", "default", "delete", "desc", "distinct", "drop", "else", "end",
    "exists", "foreign", "from", "full", "group", "having", "in", "index", "inner", "insert", "into", "is", "join",
    "key", "left", "like", "limit", "not", "null", "offset", "on", "or", "order", "outer", "primary", "references",
    "rollback", "select", "set", "table", "then", "union", "unique", "update", "values", "view", "when", "where",
    "with"};
// NOLINTNEXTLINE(reloco-no-c-array): constant table exposed to lexer_rules as a span
inline constexpr microfmt::string_view sql_operators[] = {"<>", "<=", ">=", "!=", "||", "::", "=", "<", ">", "+",
                                                          "-",  "*",  "/",  "%",  "&",  "|",  "^", "~", "!", "?",
                                                          ":"};

inline constexpr keyword_table toml_keywords{"false", "inf", "nan", "true"};
// NOLINTNEXTLINE(reloco-no-c-array): constant table exposed to lexer_rules as a span
inline constexpr microfmt::string_view toml_operators[] = {"=", "+", "-"};
// NOLINTNEXTLINE(reloco-no-c-array): constant table exposed to lexer_rules as a span
inline constexpr microfmt::string_view ini_comments[] = {"#"};
// NOLINTNEXTLINE(reloco-no-c-array): constant table exposed to lexer_rules as a span
inline constexpr microfmt::string_view ini_operators[] = {"=", ":"};
// NOLINTNEXTLINE(reloco-no-c-array): constant table exposed to lexer_rules as a span
inline constexpr microfmt::string_view kv_comments[] = {";"};
// NOLINTNEXTLINE(reloco-no-c-array): constant table exposed to lexer_rules as a span
inline constexpr microfmt::string_view kv_operators[] = {"="};

inline constexpr keyword_table json_keywords{"false", "null", "true"};

inline constexpr keyword_table shell_keywords{"case", "do", "done", "elif", "else", "esac", "fi", "for", "if", "in", "then", "while"};
// NOLINTNEXTLINE(reloco-no-c-array): constant table exposed to lexer_rules as a span
inline constexpr microfmt::string_view shell_operators[] = {"&&", "||", ";;", ">>", "<<", "|", "&", ">", "<", "="};

[[nodiscard]] constexpr lexer_rules make_c() noexcept {
  lexer_rules r;
  r.keywords = c_keywords.view();
  r.operators = c_operators;
  r.line_comment = "//";
  r.block_comment_open = "/*";
  r.block_comment_close = "*/";
  r.quotes = "\"";
  r.char_quotes = "'";
  r.string_prefixes = c_string_prefixes;
  r.number_suffixes = c_number_suffixes;
  r.digit_separator = '\'';
  r.hex_float = true;
  r.line_continuation = true;
  r.directive_prefix = '#';
  return r;
}

[[nodiscard]] constexpr lexer_rules make_cpp() noexcept {
  lexer_rules r = make_c();
  r.keywords = cpp_keywords.view();
  r.operators = cpp_operators;
  r.string_prefixes = cpp_string_prefixes;
  r.cpp_raw_strings = true;
  r.number_suffixes = {};
  r.user_defined_suffixes = true;
  return r;
}

[[nodiscard]] constexpr lexer_rules make_lua() noexcept {
  lexer_rules r;
  r.keywords = lua_keywords.view();
  r.operators = lua_operators;
  r.punctuation = "(){}[];,";
  r.line_comment = "--";
  r.long_bracket_comments = true;
  r.long_bracket_strings = true;
  r.digit_separator = '\0';
  r.hex_float = true;
  r.decimal_trailing_dot = true;
  return r;
}

[[nodiscard]] constexpr lexer_rules make_sql() noexcept {
  lexer_rules r;
  r.keywords = sql_keywords.view();
  r.keywords_ignore_case = true;
  r.operators = sql_operators;
  r.punctuation = "(),;[].";
  r.line_comment = "--";
  r.block_comment_open = "/*";
  r.block_comment_close = "*/";
  r.quotes = "'\"`";
  r.escape = '\0';
  r.doubled_quote_escape = true;
  r.multiline_quotes = "'\"`";
  r.identifier_start_extra = "@";
  r.identifier_continue_extra = "$";
  r.digit_separator = '\0';
  return r;
}

[[nodiscard]] constexpr lexer_rules make_toml() noexcept {
  lexer_rules r;
  r.keywords = toml_keywords.view();
  r.operators = toml_operators;
  r.punctuation = "[]{},.";
  r.line_comment = "#";
  r.quotes = "\"'";
  r.raw_quotes = "'";
  r.multiline_triple_quotes = "\"'";
  r.identifier_continue_extra = "-";
  r.octal_prefix = true;
  r.datetime_literals = true;
  return r;
}

[[nodiscard]] constexpr lexer_rules make_ini() noexcept {
  lexer_rules r;
  r.operators = ini_operators;
  r.punctuation = "[]";
  r.line_comment = ";";
  r.line_comments = ini_comments;
  r.quotes = "\"";
  r.escape = '\0';
  r.identifier_start_extra = "/.~$@%*+-?!&|<>^,\\()";
  r.numbers = false;
  r.digit_separator = '\0';
  r.hex_binary_prefixes = false;
  return r;
}

[[nodiscard]] constexpr lexer_rules make_config() noexcept {
  lexer_rules r;
  r.punctuation = "{};";
  r.line_comment = "#";
  r.quotes = "\"'";
  r.identifier_start_extra = "/.~$@%*+-:?!&|<>^,\\=()[]";
  r.numbers = false;
  r.digit_separator = '\0';
  r.hex_binary_prefixes = false;
  return r;
}

[[nodiscard]] constexpr lexer_rules make_config_kv() noexcept {
  lexer_rules r;
  r.operators = kv_operators;
  r.punctuation = "";
  r.line_comment = "#";
  r.line_comments = kv_comments;
  r.quotes = "\"'";
  r.identifier_start_extra = "/.~$@%*+-:?!&|<>^,\\()[]{}";
  r.numbers = false;
  r.digit_separator = '\0';
  r.hex_binary_prefixes = false;
  return r;
}

[[nodiscard]] constexpr lexer_rules make_json() noexcept {
  lexer_rules r;
  r.keywords = json_keywords.view();
  r.punctuation = "{}[],:";
  r.quotes = "\"";
  r.digit_separator = '\0';
  r.hex_binary_prefixes = false;
  return r;
}

[[nodiscard]] constexpr lexer_rules make_shell() noexcept {
  lexer_rules r;
  r.keywords = shell_keywords.view();
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

/** C17/C23: line and block comments, `#` directive lines, `'c'` characters, `L/u/U/u8` prefixes, `u/l/f` suffixes. */
inline constexpr lexer_rules c = detail::make_c();

/** C++23: like `c` plus raw strings `R"d(...)d"`, `'` digit separators, user-defined literal suffixes and `::`/`<=>`. */
inline constexpr lexer_rules cpp = detail::make_cpp();

/** Source-compatible alias of `cpp` (it used to be a generic C-like preset). */
inline constexpr lexer_rules c_like = lexer_rules(cpp);

/** Lua 5.4: `--` line and long-bracket comments, `[[ ]]` long strings, hex floats. */
inline constexpr lexer_rules lua = detail::make_lua();

/** SQL: case-insensitive keywords, `''` quote doubling, `"x"` and `` `x` `` quoted identifiers (string kind). */
inline constexpr lexer_rules sql = detail::make_sql();

/** TOML 1.0: `#` comments, literal and multi-line strings, date-times, `0o`/`0x`/`0b`. */
inline constexpr lexer_rules toml = detail::make_toml();

/** INI: `;` and `#` comments, `[`/`]` punctuation, `=` and `:` operators, values are bare words. */
inline constexpr lexer_rules ini = detail::make_ini();

/** nginx/Apache-style config: `#` comments, `{ } ;` punctuation, everything else bare words or strings. */
inline constexpr lexer_rules config = detail::make_config();

/** `key=value` config (sysctl, .env): `#`/`;` comments, `=` operator, bare-word values. */
inline constexpr lexer_rules config_kv = detail::make_config_kv();

/** JSON: only `"` strings, no comments or operators, keywords `true`/`false`/`null`. */
inline constexpr lexer_rules json = detail::make_json();

/** POSIX shell: `#` comments, `-flag`/`$var` words, redirection and logic operators. */
inline constexpr lexer_rules shell = detail::make_shell();

namespace detail {
static_assert(lexer_rules_valid(c) && lexer_rules_valid(cpp) && lexer_rules_valid(lua) && lexer_rules_valid(sql) &&
                  lexer_rules_valid(toml) && lexer_rules_valid(ini) && lexer_rules_valid(config) &&
                  lexer_rules_valid(config_kv) && lexer_rules_valid(json) && lexer_rules_valid(shell),
              "preset rules must be valid");
} // namespace detail

} // namespace microfmt::rules
