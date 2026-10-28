# Lexer guide

A *lexer* (tokenizer) turns text into a stream of small typed pieces called
*tokens*: identifiers, numbers, strings, operators and so on. You need one
whenever you read text with structure — a debugger command typed on a serial
console, a config value, an expression — or want to colour such text. Doing it
with ad-hoc `if`/`strchr` loops is error prone (off-by-one reads, endless loops
on odd input); the microfmt lexer is a small, allocation-free engine that never
reads out of bounds and always makes progress.

It is a **base engine**: it tokenizes, it does not parse grammars (build parsers with the [token stream](token-stream.md)). Include
`<microfmt/parsing/lexer.hpp>`; presets live in `lexer_rules_presets.hpp`;
token formatters and the highlighter in `lexer_formatters.hpp`.

## The three pieces

| Piece | What it is | Why you need it |
|---|---|---|
| `lexer_rules` | A plain struct describing your language (keywords, operators, comment markers, ...) | Lets one engine serve many syntaxes; everything is `constexpr` data. |
| `lexer` | A cursor over one text; `next()` returns the next `token` | Holds the position; cheap to copy rules into. |
| `token` | `kind`, `text` (a view into your source), `offset`, `line`, `column` | Nothing is copied; positions let you report errors. |

The `lexer` **borrows** the text: it must outlive the lexer and every token.
Passing a temporary `std::string` is rejected at compile time.

## Quick start

```cpp
#include <microfmt/parsing/lexer.hpp>
#include <microfmt/parsing/lexer_rules_presets.hpp>

// text to scan; must outlive the lexer and the tokens
constexpr microfmt::string_view source = "if (x >= 0x2A) return;";

// rules::c_like is a ready-made rule set (see "Presets")
microfmt::lexer lx{source, microfmt::rules::c_like};

// next() returns token_kind::end forever once the text is exhausted
for (microfmt::token t = lx.next(); t.kind != microfmt::token_kind::end; t = lx.next()) {
  // t.text is the exact slice of `source`; t.line/t.column are 1-based
}
```

`peek()` returns the next token without consuming it, `reset()` rewinds, and
`at_end()` becomes true after `next()` returned `end`. Every `next()` consumes
at least one byte (unless it returns `end`), so a loop is guaranteed to
terminate within `size + 1` calls, even on malformed input.

## Defining your own rules

```cpp
#include <microfmt/parsing/lexer.hpp>

// Keywords. The table sorts itself, so the order you write does not matter;
// a duplicate is a compile error (constexpr) or an assertion (runtime).
constexpr microfmt::keyword_table kKeywords{"let", "if", "else"};

// Operators: the longest match wins ("==" beats "="), order is irrelevant.
constexpr microfmt::string_view kOps[] = {"=", "==", "=>", "+"};

microfmt::lexer_rules make_rules() {
  microfmt::lexer_rules r;
  r.keywords = kKeywords;       // sorted_words view of the table above
  r.operators = kOps;           // span of operator spellings
  r.line_comment = "//";        // comment to end of line
  r.block_comment_open = "/*";  // both open and close, or neither
  r.block_comment_close = "*/";
  return r;
}
```

### Keyword lists: why they are sorted

Keywords are found by binary search (fast, no hashing, no allocation), which
needs them in order. `lexer_rules::keywords` is a `sorted_words` that cannot be
built from a raw span, so an unsorted list can never reach the lexer:

```cpp
// 1. keyword_table: unordered input, sorted at construction (recommended).
constexpr microfmt::keyword_table kA{"zeta", "alpha"};

// 2. You already have a sorted span of words (generated code, shared tables).
//    from_sorted verifies strict byte order: a compile error in a constant
//    expression, a RELOCO_ASSERT failure at runtime.
constexpr microfmt::string_view kSorted[] = {"alpha", "zeta"};
constexpr auto kB = microfmt::sorted_words::from_sorted(kSorted);

// 3. Same check, but returns an empty optional instead of asserting.
auto maybe = microfmt::sorted_words::try_from_sorted(kSorted);
```

`make_keyword_table("a", "b")` is the same as `keyword_table`, but is
`consteval` in C++20 so a duplicate can never reach runtime. The words must
outlive the rules (keep them `constexpr`/`static`).

## Complete rule reference

Rules are applied in this order at each position. Every rule has an opt-out.
Directives, long brackets, prefixes, raw strings and the other opt-in rules from
"Language extension rules" below slot in before the number rule.

| # | Rule | What it matches | How to disable / tune |
|---|---|---|---|
| 1 | Newline | `\n` or `\r\n`; a lone `\r` counts as whitespace | `skip_whitespace = true` (default) drops it; `false` emits `newline` tokens |
| 2 | Whitespace | Runs of space, tab, `\r`, `\v`, `\f` | Same flag; `false` emits `whitespace` tokens |
| 3 | Line comment | From `line_comment` to the end of the line | Leave `line_comment` empty; `skip_comments = false` emits `comment` tokens |
| 4 | Block comment | `block_comment_open` … `block_comment_close`; unterminated gives an `error` token to the end of input | Leave both empty; `nested_block_comments = true` makes `/* a /* b */ c */` a single comment (depth counter, no recursion) |
| 5 | String | A byte from `quotes` up to the same byte; `escape` makes the next byte literal; escapes stay raw in `text`; unterminated (end of input or raw newline) gives `error` | `quotes = ""` disables strings; `escape = '\0'` disables escapes |
| 6 | Number | Digit, or `.` followed by a digit. `integer`: `42`, `0x2A`, `0b101`; `floating`: `1.5`, `.5`, `1e9`, `1E-3`. A number glued to identifier characters (`12ab`), `0x` without digits or a trailing separator (`1_`) gives `error` | `digit_separator = '\0'` disables `1_000`; `hex_binary_prefixes = false` disables `0x`/`0b` |
| 7 | Identifier / keyword | Start: letter, `_`, any byte ≥ 0x80 (so UTF-8 names work) or a byte in `identifier_start_extra`; continue: those, digits, and `identifier_continue_extra`. Exact match in `keywords` gives `keyword` | Empty extras (default) keep the plain C rule; empty `keywords` disables keywords |
| 8 | Operator | Longest spelling in `operators` | Leave `operators` empty |
| 9 | Punctuation | One byte from `punctuation` (default `(){}[],;`) | `punctuation = ""` |
| 10 | Anything else | A one-byte `error` token | — |

`lexer_rules_valid(rules)` (constexpr) checks that comment markers are paired,
that `nested_block_comments` has an opening marker and that the identifier
extras contain no whitespace (and no digits in the start set). Use it in a
`static_assert` for rules you define as `constexpr`.

Limits worth knowing: input is bytes (no Unicode classification), the text is
one contiguous buffer, and keywords are case sensitive.

## Token kinds

`end`, `error`, `whitespace`, `newline`, `identifier`, `keyword`, `integer`,
`floating`, `string`, `comment`, `op`, `punctuation`, `character`, `directive`,
`datetime`. With
`skip_whitespace = false` and `skip_comments = false` the tokens tile the input
exactly (offsets are contiguous and cover every byte), which is what the
highlighter relies on.

## Presets

```cpp
#include <microfmt/parsing/lexer_rules_presets.hpp>

microfmt::rules::c          // C17/C23: directives, 'c' chars, L/u/U/u8 prefixes, u/l/f suffixes
microfmt::rules::cpp        // C++23: c + raw strings, 1'000 separators, 10_km literals, ::, <=>
microfmt::rules::c_like     // alias of cpp (kept for source compatibility, see changelog)
microfmt::rules::lua        // Lua 5.4: --, --[==[ ]==], [[ ]] strings, hex floats
microfmt::rules::sql        // case-insensitive keywords, '' quote doubling, "x" and `x` quoting
microfmt::rules::toml       // # comments, ' literal strings, """ multi-line, date-times
microfmt::rules::ini        // ; and # comments, [section], key = value, bare-word values
microfmt::rules::config     // nginx/Apache style: # comments, { } ; and bare words
microfmt::rules::config_kv  // key=value files (sysctl, .env): # and ; comments
microfmt::rules::json       // only " strings, true/false/null, no comments, no 0x
microfmt::rules::shell      // # comments, $var and --flag-a words, && || >> redirections
```

They are ordinary constexpr `lexer_rules`: copy one and change a field to
adapt it (`auto r = microfmt::rules::cpp; r.skip_comments = false;`).

Preset limits worth knowing (the lexer stays lenient, a parser validates):

* `sql`: `"x"` and `` `x` `` identifiers come out as `string` tokens.
* `ini`, `config`, `config_kv`: numbers are off (`numbers = false`), so `80`,
  `192.168.0.1` and `/usr/bin` are all `identifier` tokens (bare words). The
  parser decides what a value means. `ini` only treats `"` as a quote so
  `don't` is fine; `config_kv` accepts both quote kinds.
* `toml`: bare keys like `my-key` are one identifier, but `2024-key` lexes as
  `2024`, `-`, `key` (extras never glue to a number); signs are `op` tokens.
* `cpp`: `#include`/`#define` lines are one `directive` token; the parser (or a
  second lexer pass over the directive text) interprets them.

## Writing rules for a real language

The lexer is meant to sit under a parser. It does **faithful tokenization**
and leaves validation to the layer above:

* Valid source never produces an `error` token.
* It does not check escape sequences, numeric ranges, suffix validity, date
  field ranges (`2024-13-45` is a `datetime`), balanced brackets or grammar.
* With `skip_whitespace = false` and `skip_comments = false` tokens tile the
  input, with exact offsets and line/columns, so a parser can report
  diagnostics and a formatter can reproduce the source.
* `error` only means "could not continue lexing this construct" (unterminated
  string/comment, stray byte, malformed opener); each still consumes at least
  one byte so a parser can resynchronise.

## Language extension rules

These `lexer_rules` fields are off by default; every one is a pure opt-in, so
leaving it alone keeps the earlier behaviour.

| Field | What it does | Used by |
|---|---|---|
| `line_comments` | Extra line-comment markers next to `line_comment` (longest marker wins) | `ini`, `config_kv` |
| `line_continuation` | A backslash before a newline continues line comments and directives | `c`, `cpp` |
| `long_bracket_comments` | A line-comment marker followed by `[==[` starts a comment closed by the same level `]==]` | `lua` |
| `long_bracket_strings` | `[[ ... ]]` / `[==[ ... ]==]` strings (level up to 255; unterminated is `error`) | `lua` |
| `char_quotes` | Quote bytes that make `character` tokens (`'a'`) | `c`, `cpp` |
| `raw_quotes` | Quote bytes whose strings ignore the escape byte | `toml` (`'`) |
| `doubled_quote_escape` | A doubled quote inside a string is a literal quote (`'it''s'`) | `sql` |
| `multiline_triple_quotes` | Quote bytes that also allow `"""..."""` / `'''...'''` strings spanning lines | `toml` |
| `multiline_quotes` | Quote bytes whose ordinary strings may contain raw newlines | `sql` |
| `string_prefixes` | Prefixes such as `u8`, `L`, `R` that join a directly following quote | `c`, `cpp` |
| `cpp_raw_strings` | A prefix ending in `R` starts `R"delim( ... )delim"` (delimiter at most 16 bytes) | `cpp` |
| `number_suffixes` | Known suffixes (`u`, `ul`, `f`, ...), ASCII case-insensitive, longest wins | `c` |
| `user_defined_suffixes` | Any identifier run after a number, string or char joins the token (`10_km`, `"s"_str`) | `cpp` |
| `hex_float` | `0x1.8p3` | `c`, `cpp`, `lua` |
| `octal_prefix` | `0o17` | `toml` |
| `datetime_literals` | `1979-05-27`, `07:32:00.5`, `1979-05-27T07:32:00Z`, `... 07:32:00+01:00` as one `datetime` token | `toml` |
| `numbers` | `false` disables the number rule: digits start identifiers | `ini`, `config`, `config_kv` |
| `keywords_ignore_case` | Keyword lookup folds ASCII case; the table must be lower-case (checked by `lexer_rules_valid`) | `sql` |
| `directive_prefix` | A first-on-line `#` makes the whole line (with continuations) one `directive` token | `c`, `cpp` |

Examples (each field with what it is, why and what the values mean):

```cpp
// A language with ';' and '#' comments, "x" strings without escapes and
// case-insensitive keywords. Keywords must be lower-case when
// keywords_ignore_case is set, otherwise lexer_rules_valid() is false.
constexpr microfmt::keyword_table kWords{"select", "from"};
constexpr microfmt::string_view kExtraComments[] = {"#"};

constexpr microfmt::lexer_rules make() {
  microfmt::lexer_rules r;
  r.keywords = kWords;
  r.keywords_ignore_case = true;     // "SELECT" and "Select" are keywords too
  r.line_comment = ";";              // first marker
  r.line_comments = kExtraComments;  // more markers; the longest match wins
  r.escape = '\0';                   // no escape byte: a backslash is plain text
  r.quotes = "\"";                   // only double quotes open strings
  return r;
}
static_assert(microfmt::lexer_rules_valid(make()));
```

`lexer_rules_valid` additionally rejects: long-bracket comments with no
line-comment marker, `cpp_raw_strings` without a prefix ending in `R`,
`char_quotes` overlapping `quotes`, empty prefixes/suffixes/markers, a
`directive_prefix` that is also the first byte of a line-comment marker, and a
non-lower-case table with `keywords_ignore_case`.

## Changelog

* `rules::c_like` is now an alias of `rules::cpp`. Visible differences:
  `'a'` is a `character` token (it used to be `string`), `#...` lines are one
  `directive` token, `10UL`/`1.5f` no longer produce `error`.
* New `token_kind` values (`character`, `directive`, `datetime`) were appended
  after the existing ones, so stored numeric values are unchanged.
* A number followed by `-` or `.` extras (`2024-key`) is no longer an error
  token; only letters, digits, `_` and high bytes glue to a number.

## Formatting tokens and highlighting

`<microfmt/parsing/lexer_formatters.hpp>` adds:

```cpp
#include <microfmt/parsing/lexer_formatters.hpp>

// {} on a token_kind prints its name; {} on a token prints
//   kind "escaped text" @line:column   (control bytes are escaped, safe to log)
microfmt::format_to(out, "{} {}", tok.kind, tok);

// highlight(text, rules, theme) renders ANSI-coloured text. Whitespace and
// comments are kept, so removing the escape codes gives back the input.
// The text must outlive the view; theme is optional.
microfmt::format_to(out, "{}", microfmt::highlight(source, microfmt::rules::c_like));

// Custom colours: a default-constructed ansi::style prints that class unstyled.
microfmt::highlight_theme theme;
theme.keyword = {microfmt::ansi::color::bright_red};
theme.comment = {};
microfmt::format_to(out, "{}", microfmt::highlight(source, microfmt::rules::c_like, theme));
```

Colours are skipped entirely when `microfmt::ansi::style::colors_enabled` is
false (for serial or non-TTY sinks).

## Integration notes

* **Logging:** format `token` values in debug logs to see exactly what the
  lexer produced and where.
* **Sinks:** `highlight()` writes to any sink; pair it with `styled_sink` or a
  console sink. Use `buffer_sink<N>` for heap-free capture.
* **Bare metal:** no heap, no exceptions, `noexcept` throughout, rule tables
  can live in flash as `constexpr` data (see `docs/bare-metal.md`).
* **Safety:** `lexer`, `token` and `highlight_view` borrow their input
  (`RELOCO_POINTER`); constructors are `RELOCO_LIFETIMEBOUND` and rvalue
  `std::string` sources are deleted (see `docs/lifetime-safety.md`).

See `examples/lexer_demo.cpp` for a runnable demo.
