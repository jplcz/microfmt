# Design: base lexer engine (`microfmt/parsing/lexer.hpp`)

Status: **Implemented** (user guide: [../lexer.md](../lexer.md))

## 1. Why a lexer in a formatting library?

A *lexer* (tokenizer) splits text into meaningful pieces — identifiers,
numbers, strings, operators — called *tokens*. Many microfmt users end up
writing the same ad-hoc scanning loop by hand:

* parsing small command lines, config values or debugger commands on a device
  with no heap (the inspector/GDB-protocol code already does this privately);
* colouring or re-formatting text for logs and consoles (microfmt already has
  ANSI/styled output but nothing that knows *what* to colour);
* validating or pretty-printing snippets such as expressions or format
  strings.

A shared, allocation-free engine removes that duplication and gives all of
these a single, well-tested, bounds-safe foundation. It is a **base engine**:
it does not parse grammars, it only produces tokens from a rule table.

### Goals

* No heap allocation, no exceptions, `noexcept` everywhere, usable on
  bare-metal (see `docs/bare-metal.md`).
* Rules described by a `constexpr` table (hand-written, no regex).
* Tokens are views into the caller's text; nothing is copied.
* Never reads out of bounds, never loops forever on malformed input.
* Ships token formatters and a syntax highlighter built on `styled.hpp`.

### Non-goals (v1)

* No regex or compile-time pattern language.
* No parser, AST, or grammar support.
* No Unicode classification: input is bytes; bytes ≥ 0x80 are treated as
  identifier characters so UTF-8 identifiers/strings pass through intact.
* No incremental/streaming state across buffers (the caller passes one
  contiguous text).

## 2. Concepts

```
source text:   let x1 = 0x2A + "hi"; // note
tokens:        keyword  identifier  operator  integer  operator  string  punctuation  comment
```

| Term | Meaning |
|---|---|
| **Rules** (`lexer_rules`) | A small constexpr struct describing the language: keywords, operators, comment markers, quote characters, number flavours. |
| **Lexer** (`lexer`) | A cheap cursor over one text. `next()` returns the next token. |
| **Token** | `kind` + `text` (a view into the source) + position (`offset`, `line`, `column`). |

## 3. Public API (proposed)

```cpp
namespace microfmt {

/** Category of a token. `end` is returned forever once input is exhausted. */
enum class token_kind : uint8_t {
  end,         // no more input
  error,       // unterminated string/comment or unknown byte
  whitespace,  // only produced when rules.skip_whitespace == false
  newline,     // only produced when rules.skip_whitespace == false
  identifier,
  keyword,     // identifier found in rules.keywords
  integer,     // 42, 0x2A, 0b101, 1_000 (when digit_separator is set)
  floating,    // 1.5, 1e9, .5
  string,      // "text" or 'text' incl. quotes; escapes kept raw
  comment,     // only produced when rules.skip_comments == false
  op,          // longest match from rules.operators
  punctuation  // single byte from rules.punctuation
};

/** One token. `text` borrows from the lexed source; it must not outlive it. */
struct RELOCO_POINTER token {
  token_kind kind{token_kind::end};
  microfmt::string_view text{};
  size_t offset{0};   // byte offset of text[0] in the source
  uint32_t line{1};   // 1-based
  uint32_t column{1}; // 1-based, in bytes
};

/** Language description. All members are views of static (constexpr) data. */
struct lexer_rules {
  sorted_words keywords{};                       // exact, case-sensitive; sorted by construction (see below)
  span<const microfmt::string_view> operators{}; // longest match wins; order irrelevant
  microfmt::string_view punctuation{"(){}[],;"}; // single-byte tokens
  microfmt::string_view line_comment{};          // e.g. "//" or "#"; empty = none
  microfmt::string_view block_comment_open{};    // e.g. "/*"
  microfmt::string_view block_comment_close{};   // e.g. "*/"
  bool nested_block_comments{false};             // "/* a /* b */ c */" is one comment
  microfmt::string_view identifier_start_extra{};    // extra start bytes, e.g. "$@"
  microfmt::string_view identifier_continue_extra{}; // extra bytes after the first, e.g. "-." for shell flags
  microfmt::string_view quotes{"\"'"};           // each byte starts a string
  char escape{'\\'};                             // 0 = no escapes
  char digit_separator{'_'};                     // 0 = disabled
  bool hex_binary_prefixes{true};                // 0x.. and 0b..
  bool skip_whitespace{true};
  bool skip_comments{true};
};

class RELOCO_POINTER lexer {
public:
  /** Rejects temporaries so the cursor can never dangle. */
  explicit constexpr lexer(microfmt::string_view source RELOCO_LIFETIMEBOUND,
                           const lexer_rules &rules) noexcept;
  explicit lexer(std::string &&, const lexer_rules &) = delete;

  /** Consumes and returns the next token. */
  [[nodiscard]] token next() noexcept;
  /** Returns the next token without consuming it. */
  [[nodiscard]] token peek() const noexcept;
  /** Rewinds to the start of the source. */
  void reset() noexcept;
  /** True once `next()` has returned `token_kind::end`. */
  [[nodiscard]] bool at_end() const noexcept;
};

} // namespace microfmt
```

### Keyword tables: sorted by construction

Keywords are binary-searched, so they must be sorted. Instead of asking users
to keep a table sorted (and checking it), `lexer_rules::keywords` has the type
`sorted_words`, a tiny non-owning view that can **only** be obtained from a
source that guarantees order:

```cpp
/** Non-owning view of a sorted, duplicate-free word list. The only way to
 *  obtain one is a factory below or from a keyword_table; there is no public
 *  constructor from a raw span. */
class RELOCO_POINTER sorted_words {
public:
  constexpr sorted_words() noexcept = default; // empty list

  /** Checked tier: `words` (any array/C array/reloco::array converts to a span)
   *  must already be sorted in byte order and duplicate-free. Verified on every
   *  call; in a constant expression a violation is a compile error, at runtime
   *  it fails RELOCO_ASSERT. Nothing is copied or re-sorted; the result
   *  borrows the span's storage. */
  [[nodiscard]] static constexpr sorted_words from_sorted(
      span<const microfmt::string_view> words RELOCO_LIFETIMEBOUND) noexcept;

  /** try_ tier: same check, but returns an empty optional instead of asserting. */
  [[nodiscard]] static constexpr reloco::optional<sorted_words> try_from_sorted(
      span<const microfmt::string_view> words RELOCO_LIFETIMEBOUND) noexcept;

  [[nodiscard]] constexpr bool contains(microfmt::string_view word) const noexcept; // binary search
  [[nodiscard]] constexpr size_t size() const noexcept;
};

/** Owning, fixed-size, always sorted and duplicate-free. Sorts itself when built. */
template <size_t N> class keyword_table {
public:
  /** Order of arguments is irrelevant; duplicates are a compile error in a
   *  constant-evaluated context (C++20: `make_keyword_table` is consteval) and
   *  a RELOCO_ASSERT failure at runtime. */
  template <typename... Words> explicit constexpr keyword_table(Words... words) noexcept;

  [[nodiscard]] constexpr sorted_words view() const & noexcept RELOCO_LIFETIMEBOUND;
  sorted_words view() const && = delete; // would dangle
  constexpr operator sorted_words() const & noexcept RELOCO_LIFETIMEBOUND; // so lexer_rules{.keywords = table}
};

template <typename... Words> keyword_table(Words...) -> keyword_table<sizeof...(Words)>;
```

Two ways to supply keywords:

```cpp
// 1. Recommended: order doesn't matter, the table sorts itself at build time.
constexpr microfmt::keyword_table kKeywords{"let", "if", "else"};

// 2. You already have a sorted span of words (e.g. generated or shared with other
//    code). It must be sorted; the check runs at runtime (or compile time if
//    constexpr). The span's storage must outlive the rules.
constexpr microfmt::string_view kSorted[] = {"else", "if", "let"};
constexpr auto kWords = microfmt::sorted_words::from_sorted(kSorted);

constexpr microfmt::lexer_rules rules{.keywords = kKeywords /* or kWords */};
```

Because `sorted_words` has no public constructor from a raw span, an unsorted
table cannot reach the lexer, so the lexer needs no release-build fallback
and `lexer_rules_valid()` only validates the remaining fields (comment and
quote consistency). `operators` keep the plain span (the lexer scans all of
them for the longest match, so order does not matter).

`lexer_rules` is stored by value inside the lexer (it only holds views), so
the lexer owns no raw pointers. Predefined rule sets (`rules::c_like`,
`rules::json`, `rules::shell`) live in `parsing/lexer_rules_presets.hpp` and are
examples as much as features.

### Usage example (documented in the guide)

```cpp
#include <microfmt/parsing/lexer.hpp>

// Keywords the language reserves; constexpr so they live in flash. The table
// sorts itself, so the order written here does not matter.
static constexpr microfmt::keyword_table kKeywords{"let", "if", "else"};
static constexpr microfmt::string_view kOps[] = {"==", "=", "+", "-", "->"};

constexpr microfmt::lexer_rules rules{.keywords = kKeywords, .operators = kOps,
                                      .line_comment = "//"};
static_assert(microfmt::lexer_rules_valid(rules)); // remaining field consistency

microfmt::lexer lx{"let x = 0x2A; // hi", rules}; // text must outlive lx
for (auto t = lx.next(); t.kind != microfmt::token_kind::end; t = lx.next()) {
  // {:>12} pads the kind name; text is the exact source slice
  microfmt::format_to(out, "{:>12} {}\n", t.kind, microfmt::quoted(t.text));
}
```

## 4. Lexing rules (precise behaviour)

Applied in this order at each position:

1. **Whitespace/newline** — skipped when `skip_whitespace`, else emitted as one
   token per run (newline tokens are a single `\n` or `\r\n`).
2. **Comments** — `line_comment` runs to (not including) the newline;
   block comments run to `block_comment_close`; with `nested_block_comments`
   each further `block_comment_open` must be closed too (nesting depth is a
   counter, no recursion or stack). Skipped when `skip_comments`. An
   unterminated block comment yields `error` covering the rest of the input.
3. **Strings** — a byte in `quotes` starts a string ending at the same quote.
   If `escape != 0`, the escape byte makes the next byte literal. Unterminated
   (end of input or raw newline) yields `error` covering the consumed text.
4. **Numbers** — digit, or `.` followed by digit. Handles prefixes,
   fractional part, exponent (`e`/`E`, optional sign) and `digit_separator`.
   A trailing identifier character (`12ab`) yields `error`.
5. **Identifiers/keywords** — start: `[A-Za-z_]`, a byte ≥ 0x80, or a byte in
   `identifier_start_extra`; continue: start bytes plus digits plus
   `identifier_continue_extra`. Promoted to `keyword` on exact match.
6. **Operators** — longest match from `operators`.
7. **Punctuation** — single byte in `punctuation`.
8. **Anything else** — one-byte `error` token. Progress is guaranteed: every
   `next()` call consumes at least one byte unless it returns `end`.

Complexity: O(token length × |operators| + log |keywords|) per token. Keywords
are binary-searched; sortedness is guaranteed by the `sorted_words` type (see
§3), not by a runtime fallback. `lexer_rules_valid(rules)` (constexpr) checks
that comment/quote settings are consistent.

## 5. Formatters and highlighter

* `formatter<token_kind>` — lowercase name (`identifier`).
* `formatter<token>` — `kind "text" @line:column`, with `text` escaped via the
  existing `escaped.hpp` helper so control bytes are safe to log.
* `highlight(text, rules, theme)` returns a view that renders the text with
  ANSI colours per token kind via `ansi.hpp`/`styled_sink.hpp`. Whitespace and
  comments are preserved (the view lexes with both skip flags off) so the
  output is byte-identical to the input minus escape codes. `theme` maps
  `token_kind` → ANSI style, with a built-in default.

These live in `parsing/lexer_formatters.hpp` so users of the bare engine do not pay
for formatter/ANSI code.

## 6. Safety and project guidelines

To be re-verified against `docs/lifetime-safety.md`,
`docs/hardened-containers.md` and `docs/extending.md` before implementation:

* `lexer`, `token` and the highlight view are `RELOCO_POINTER` types (they
  borrow); constructors use `RELOCO_LIFETIMEBOUND`; rvalue sources are
  `= delete`d (aliasing accessors are `&`/`const &` qualified).
* No raw pointers in the public API: views, spans and indices only. Internal
  indexing is range-checked; where the unsafe-buffer warning requires, the
  access is wrapped in `RELOCO_BEGIN/END_UNSAFE_BUFFER_USAGE` with the bound
  stated in a comment.
* Everything is `noexcept`; no heap; `constexpr` where possible so rule
  tables and small tokenizations can be verified at compile time.
* Prefer reloco/microfmt types (`microfmt::string_view`, `span`) over STL.
* Plain tier only in v1 (no `try_`/`unsafe_` variants needed: nothing fails
  except via `token_kind::error`).

## 7. Files

| File | Purpose |
|---|---|
| `include/microfmt/parsing/lexer.hpp` | `token_kind`, `token`, `lexer_rules`, `lexer` |
| `include/microfmt/parsing/lexer_rules_presets.hpp` | `c_like`, `json`, `shell` rule sets |
| `include/microfmt/parsing/lexer_formatters.hpp` | `token_kind`/`token` formatters, `highlight()` |
| `tests/test_lexer.cpp` | engine tests |
| `tests/test_lexer_formatters.cpp` | formatter/highlighter tests |
| `include/microfmt/parsing/README.md` | what the parsing directory contains (non-obvious directory) |
| `docs/lexer.md` | user guide (API, examples, **every rule** and how to opt out of each) |
| `examples/lexer_demo.cpp` | tokenizer + highlighter demo, plus VS Code launch/task entries |
| `tests/compile_all_headers.cpp`, `CMakeLists.txt` | registration |

No Lua helpers: microfmt does not expose Lua.

## 8. Test plan

* Each rule in §4 with a positive and a negative case.
* `keyword_table` sorts any argument order; duplicates rejected; `sorted_words::from_sorted`
  accepts a sorted span, `try_from_sorted` returns empty for unsorted or
  duplicate input; keyword binary search finds first/last/missing words.
* A `static_assert` on a constexpr table, and a `requires`-style compile-fail
  check that `lexer_rules` cannot be built from a raw span.
* Nested and non-nested block comments; custom identifier bytes.
* Operator longest-match (`=` vs `==` vs `=>`).
* Unterminated string/block comment, stray byte, `12ab`.
* Positions: `line`/`column`/`offset` across `\n`, `\r\n`, and tokens at EOF.
* Progress guarantee: loop over every byte value and pseudo-random short
  inputs asserting `next()` terminates within `size + 1` calls and tokens
  tile the input exactly when whitespace/comments are not skipped.
* `peek()` equals subsequent `next()`; `reset()` restarts.
* Highlight output with escape codes stripped equals the input.
* Compiles in C++17/20/23 header checks; runs under the existing sanitizer
  build.

## 9. Decisions (resolved open questions)

1. **Keyword lookup:** binary search over a table that is sorted by
   construction (`keyword_table`), plus checked/`try_` factories for an
   existing sorted `span` that verify order (compile time when constexpr,
   otherwise at runtime).
2. **Identifier character classes:** supported in v1 through
   `identifier_start_extra` / `identifier_continue_extra`.
3. **Nested block comments:** supported in v1 through `nested_block_comments`.
4. **Placement:** new directory `include/microfmt/parsing/` (room for future
   parsing helpers); the formatters/highlighter live there too.

## 10. Rollout

1. Design accepted.
2. Implement engine + tests, then formatters/highlighter, then demo/docs.
3. Run reloco-tidy and the full test matrix; update `docs/` index and flip
   status to Implemented.
