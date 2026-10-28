# Design: language coverage for the lexer (C, C++, Lua, SQL, INI, TOML, config)

Status: **Implemented** (extends [lexer.md](lexer.md); user guide: [../lexer.md](../lexer.md))

## 1. Why

The base lexer ships with `c_like`, `json` and `shell` presets, but it cannot
describe several very common syntaxes faithfully:

| Language | What the engine cannot express today |
|---|---|
| C / C++ | `'c'` char literals as their own kind, number suffixes (`10UL`, `1.5f`), `'` digit separators, string prefixes (`u8"x"`, `L'x'`), raw strings `R"d(...)d"`, `#include` directive lines, `\`-continued comments/directives, hex floats |
| Lua | `--` comments, long brackets `[==[ ... ]==]` for strings **and** comments, hex floats |
| SQL | case-insensitive keywords, `'it''s'` doubled-quote escape, `"ident"` and `` `ident` `` quoting, `--` comments |
| INI | two comment markers (`;` and `#`) |
| TOML | `'literal'` strings without escapes, `"""` / `'''` multi-line strings, date-times, `0o`, `1_000`, bare keys with `-` |
| config | `#` comments with bare words (nginx-like) and `key=value` (sysctl-like) |

Without these, highlighting and tokenizing real files produces `error` tokens
on valid input. This document adds the missing engine rules and defines the
presets. It does not change what existing presets produce except where noted.

### Non-goals

* Parsing grammars, indentation tracking (Python), YAML (see the earlier
  discussion; needs INDENT/DEDENT and context, out of scope).
* Full spec conformance. Presets are *highlighter-grade*: they accept valid
  input and never loop or overrun; they may accept some invalid input.
* Unicode classification (bytes ≥ 0x80 stay identifier bytes).

### Contract for real parsers

The lexer is meant to sit under an actual parser for these languages. Its job
is **faithful tokenization**, not validation:

* **No false errors:** valid source never produces an `error` token. Anything
  that is lexically ambiguous is accepted and left to the parser.
* **Lenient on content:** the lexer does not check escape sequences, numeric
  range or digit validity for a suffix, date/time field ranges (`2024-13-45`
  is a `datetime`), identifier legality after keywords, balanced brackets,
  directive contents or SQL/TOML grammar. Those belong to the upper layer.
* **Lossless:** with `skip_whitespace` and `skip_comments` off, tokens tile the
  input exactly, with accurate offset/line/column, so a parser can report
  diagnostics and a formatter can reproduce the source.
* **Errors only mean "cannot continue lexing this construct":** unterminated
  string/comment/raw string, a stray byte, or a malformed opener. Each still
  consumes at least one byte so a parser can resynchronise.

## 2. New token kinds

Appended to `token_kind` (existing values keep their order):

| Kind | Meaning |
|---|---|
| `character` | A quoted char literal from `char_quotes`, including prefix and quotes (`'a'`, `u8'a'`) |
| `directive` | A whole directive line starting with `directive_prefix` at line start (after blanks), e.g. `#include <x>`; continues over `\`+newline |
| `datetime` | TOML-style date/time literal (`1979-05-27`, `07:32:00`, `1979-05-27T07:32:00Z`) |

`token_kind_name`, the formatter and `highlight_theme` gain matching entries:
`character` uses the `string` style, `directive` has its own style
(`directive`, default bright blue), `datetime` uses `number`.

## 3. New `lexer_rules` fields

All default to "off", so existing rules behave identically.

```cpp
struct lexer_rules {
  // ... existing fields ...

  // Comments
  span<const string_view> line_comments{};   // several markers (INI: ";" and "#")
  bool line_continuation{false};             // trailing '\' joins the next line (comments, directives)
  bool long_bracket_comments{false};         // Lua: line_comment followed by [=*[ ... ]=*]

  // Strings and chars
  string_view char_quotes{};                 // bytes that open a character literal ("'" in C)
  string_view raw_quotes{};                  // quote bytes whose strings have no escapes (TOML ')
  bool doubled_quote_escape{false};          // SQL: '' inside '...' is a literal quote
  string_view multiline_triple_quotes{};     // quote bytes allowing """ ... """ (TOML)
  bool long_bracket_strings{false};          // Lua: [[ ... ]] and [=*[ ... ]=*]
  span<const string_view> string_prefixes{}; // "u8", "u", "U", "L", "R", "u8R", ... directly before a quote
  bool cpp_raw_strings{false};               // a prefix ending in 'R' starts R"delim( ... )delim"
  string_view multiline_quotes{};            // bytes whose single-quoted strings may span newlines

  // Numbers
  span<const string_view> number_suffixes{}; // "u","l","ul","ll","ull","f","z","uz", ... (ASCII case-insensitive)
  bool user_defined_suffixes{false};         // C++ UDLs: any identifier run after a number/string/char joins the token (10_km, "x"_s)
  bool hex_float{false};                     // 0x1.8p3 (C99/C++17/Lua)
  bool octal_prefix{false};                  // 0o17 (TOML, Rust-like)
  bool numbers{true};                        // false: digits start identifiers (INI/config bare words)
  bool datetime_literals{false};             // TOML date/time

  // Keywords
  bool keywords_ignore_case{false};          // ASCII fold; the table must be stored lower-case

  // Directives
  char directive_prefix{'\0'};               // '#' for C/C++ preprocessor
};
```

### 3.1 Rule semantics

Applied in this order at each position, extending the order in
[../lexer.md](../lexer.md):

1. Newline / whitespace (unchanged).
2. **Directive**: if `directive_prefix` is set and this is the first
   non-blank byte of the line, the rest of the line (continued by `\`+newline
   when `line_continuation`) is one `directive` token. Comments inside a
   directive stay part of it (highlighter-grade). Beats comments because `#`
   may also be a comment marker in other presets (not both in one preset).
3. **Line comments**: every marker in `line_comment` / `line_comments`. With
   `long_bracket_comments`, a marker directly followed by a long-bracket opener
   (Lua `--[==[`) starts a block comment closed by the matching level.
   `line_continuation` extends a line comment over `\`+newline.
4. Block comment (unchanged).
5. **Strings/chars**, in this order:
   a. `string_prefixes`: longest prefix directly followed by a string or char
      quote becomes part of the token. Prefix only counts when the preceding
      byte is not an identifier byte (so `fooL"x"` is identifier + string).
   b. `cpp_raw_strings`: `R"delim(` … `)delim"`; delimiter ≤ 16 bytes; no
      newline restriction; unterminated → `error` to the end of input.
   c. `long_bracket_strings`: `[` `=`* `[` … `]` `=`* `]` with the same count of
      `=`; unterminated → `error`. A `[` not followed by `=`* `[` is punctuation.
   d. Triple quotes: `"""` / `'''` for quotes in `multiline_triple_quotes`;
      ends at the next unescaped triple (escape only for non-raw quotes).
   e. Ordinary strings: `quotes`; `raw_quotes` disables the escape byte;
      `doubled_quote_escape` treats a doubled quote as content; strings end at
      a raw newline (error) unless the quote is in `multiline_quotes`.
   f. `char_quotes`: like an ordinary string, ends at a raw newline (error),
      token kind `character`.
6. **Numbers**: as before plus: `'` is accepted as a digit separator when
   `digit_separator == '\''` (only between digits, so it never collides with
   char quotes); `hex_float` (`0x1.8p+3`); `octal_prefix`; then an optional
   suffix from `number_suffixes` (longest wins, ASCII case-insensitive). With
   `user_defined_suffixes` any identifier run after a number, string or char
   joins the token (C++ `10_km`, `"x"_s`; the parser validates it). Otherwise
   an identifier byte glued to the number is `error`.
   `datetime_literals`: a digit run matching `dddd-dd-dd`, `dd:dd:dd[.frac]`
   or their `T`/space/`Z`/`±dd:dd` combinations is one `datetime` token; tried
   before plain numbers.
7. Identifier / keyword: `keywords_ignore_case` folds ASCII on comparison
   (`sorted_words::contains_ignore_case`, no allocation; the table holds
   lower-case words, enforced at construction for `keyword_table`).
8. Operator, punctuation, anything-else error: unchanged.

### 3.2 Validity (`lexer_rules_valid`)

Extended to reject: `long_bracket_comments` without any `line_comment(s)`;
`cpp_raw_strings` without a prefix ending in `R`; `char_quotes` overlapping
`quotes`; `directive_prefix` that is also the first byte of a line comment
marker; `keywords_ignore_case` with an upper-case keyword; empty strings in
`string_prefixes` / `number_suffixes` / `line_comments`.

### 3.3 Safety and limits

* Every scan is bounded by the source size; each `next()` still consumes at
  least one byte (tiling property is preserved and re-tested over all 256 byte
  values for every preset).
* Raw-string delimiter ≤ 16 and long-bracket level ≤ 255 (beyond that the
  opener is not recognised and falls back to punctuation/error), so scans stay
  linear (no backtracking beyond one closing-marker comparison per byte).
* No allocation, no recursion, `noexcept`, all new fields are views/spans
  (`RELOCO_POINTER` lifetime already covers `lexer_rules`).

## 4. Presets

All are `inline constexpr lexer_rules` in `lexer_rules_presets.hpp`, each with
`static_assert(lexer_rules_valid(...))`.

| Preset | Notes |
|---|---|
| `rules::c` | C17: `//`, `/* */`, `#` directives, `'c'`, `L/u/U/u8` prefixes, suffixes `u l ll ul ull f`, hex float, C keywords incl. `_Bool`, `inline`, `restrict`, `volatile`, `register`, `goto`, `extern`, `auto`, `signed`, `unsigned`, `void`, `char`, `short`, `int`, `long`, `float`, `double` |
| `rules::cpp` | C++23: `c` plus `user_defined_suffixes`, `R"..."` raw strings, `'` digit separator, `0b`, suffixes `z uz`, operators `::`, `->*`, `.*`, `<=>`, `...`, C++ keywords (`class`, `template`, `constexpr`, `co_await`, ...) |
| `rules::c_like` | **Alias of `rules::cpp`** for source compatibility. Behaviour change: `'a'` is now `character`, directives become one token, suffixes are accepted. Documented in the guide's changelog. |
| `rules::lua` | `--` comments, `--[==[ ]==]`, `[[ ]]` strings, `"`/`'` strings with `\` escapes, hex float, operators `.. ... == ~= <= >= // :: << >> `, keywords `and break do else elseif end false for function goto if in local nil not or repeat return then true until while` |
| `rules::sql` | `--` and `/* */` (non-nested), `'` strings with `''`, `"` and `` ` `` quoted identifiers lexed as `string` (§8), `keywords_ignore_case`, common ANSI keywords, operators `<> <= >= != \|\| :: =`, `digit_separator` off |
| `rules::ini` | `;` and `#` comments, `[`/`]`/`=`/`:` punctuation/op, `"` and `'` strings without escapes, identifier extras `-.` so keys like `log-level` are one word, no keywords, no numeric prefixes |
| `rules::toml` | `#` comments, `"` strings with escapes, `'` raw strings, `"""`/`'''` multi-line, `datetime_literals`, `octal_prefix`, `0x`/`0b`, `_` separators, keywords `true false inf nan`, bare-key extras `-` , punctuation `[]{},=.` |
| `rules::config` | nginx/Apache-like: `#` comments, `"`/`'` strings, `{ } ;` punctuation, identifier extras `-./:*$@` so paths/words are one token, no keywords |
| `rules::config_kv` | key=value `.conf` (sysctl/env-like): `#` and `;` comments, `=`/`:` operators, same word extras, strings optional |

## 5. Formatters and highlighter

* `token_kind_name` / `formatter<token_kind>` gain `character`, `directive`,
  `datetime`.
* `highlight_theme` gains `directive`, (`character` shares `string`,
  `datetime` shares `number`).
* No changes to the highlighter algorithm; it already tiles on `token.text`.

## 6. Compatibility

* New fields default off → existing user-defined rules are unchanged.
* `token_kind` only grows at the end, so stored numeric values stay valid.
* `rules::json` and `rules::shell` are untouched.
* `rules::c_like` changes behaviour (see §4); this is the only intentional
  output change and it is called out in `docs/lexer.md`.

## 7. Files and tests

Files (all under the existing layout):

* `include/microfmt/parsing/lexer.hpp`: new kinds, fields, scan logic,
  `sorted_words::contains_ignore_case`.
* `include/microfmt/parsing/lexer_rules_presets.hpp`: new presets.
* `include/microfmt/parsing/lexer_formatters.hpp`: names/theme.
* `docs/lexer.md`: new fields in the rule reference, presets table, changelog;
  `docs/design/README.md` status.
* `tests/test_lexer.cpp`, `tests/test_lexer_languages.cpp` (new, registered in
  `CMakeLists.txt`), `examples/lexer_demo.cpp` extended.

Tests:

* Per feature: prefix/suffix cases, raw strings (delimiter, nested `)`,
  unterminated), long brackets (levels 0..2, mismatched level), triple quotes,
  doubled quotes, `''`-escape, continuation lines, directive vs `#` comment,
  case-insensitive keyword lookup, datetime forms.
* Per preset: a realistic snippet with an exact expected kind sequence, and a
  sample of tricky valid input that must yield **zero** `error` tokens.
* Property test over all 256 byte values × every preset: tokens tile the input
  and the lexer terminates (extends the existing test).
* `lexer_rules_valid` negative cases.

## 8. Decisions

| Question | Decision |
|---|---|
| New token kinds | `character`, `directive`, `datetime` |
| SQL keywords | `keywords_ignore_case` flag, ASCII fold, no allocation |
| Preset names | add `c cpp lua sql ini toml config config_kv`; `c_like` kept as alias of `cpp` |
| `config` flavor | both: `config` (nginx-like) and `config_kv` (key=value) |
| SQL quoted identifiers | `"x"` and `` `x` `` reuse the `string` kind (no extra field; documented limitation) |
| Number gluing | Only letters, digits, `_` and high bytes glue to a number (`2024-key` is `2024`, `-`, `key`); identifier extras do not, so valid TOML/config text never errors |
| Config values | INI/config presets turn numbers off (`numbers = false`) so `192.168.0.1` and `/usr/bin` are one bare word |
