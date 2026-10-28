# Lexer guide

A *lexer* (tokenizer) turns text into a stream of small typed pieces called
*tokens*: identifiers, numbers, strings, operators and so on. You need one
whenever you read text with structure — a debugger command typed on a serial
console, a config value, an expression — or want to colour such text. Doing it
with ad-hoc `if`/`strchr` loops is error prone (off-by-one reads, endless loops
on odd input); the microfmt lexer is a small, allocation-free engine that never
reads out of bounds and always makes progress.

It is a **base engine**: it tokenizes, it does not parse grammars. Include
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
`floating`, `string`, `comment`, `op`, `punctuation`. With
`skip_whitespace = false` and `skip_comments = false` the tokens tile the input
exactly (offsets are contiguous and cover every byte), which is what the
highlighter relies on.

## Presets

```cpp
#include <microfmt/parsing/lexer_rules_presets.hpp>

microfmt::rules::c_like  // // and /* */ comments, " and ' strings, 0x/0b, C operators
microfmt::rules::json    // only " strings, true/false/null, no comments, no 0x
microfmt::rules::shell   // # comments, $var and --flag-a words, && || >> redirections
```

They are ordinary constexpr `lexer_rules`: copy one and change a field to
adapt it (`auto r = microfmt::rules::c_like; r.skip_comments = false;`).

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

See `examples/lexer_demo.cpp` for a runnable demo and `docs/design/lexer.md`
for the design rationale.
