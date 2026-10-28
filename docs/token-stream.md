# Token stream guide (building parsers on the lexer)

The [lexer](lexer.md) gives you tokens; a *parser* decides what a sequence of
tokens **means** (is this a valid JSON document? what command did the user
type?). `token_stream` is the small helper that makes writing such a parser by
hand short and safe. You need it because every parser otherwise repeats the
same fiddly glue — "is the next token a `(`? consume it, else report the
line/column" — and hand-rolled versions tend to report wrong positions or loop
forever on bad input.

It is deliberately **not** a parser generator: no grammar DSL, no AST type.
Your own functions are the grammar (the *recursive-descent* style: one function
per rule, rules call each other). That keeps it allocation-free and usable on
bare-metal. Include `<microfmt/parsing/token_stream.hpp>`.

## The pieces

| Piece | What it is | Why you need it |
|---|---|---|
| `token_stream` | A lexer plus two cached lookahead tokens | `peek()`/`peek2()` let you choose a rule without consuming input. |
| `parse_error` | A static `message` plus the offending `token` | Gives you line/column for error reports for free. |
| `depth_guard` | RAII recursion counter | Hostile input like `[[[[...` cannot overflow a small stack. |

Like the lexer, the stream **borrows** the text; it must outlive the stream and
every token. A temporary `std::string` is rejected at compile time.

## API

```cpp
#include <microfmt/parsing/lexer_rules_presets.hpp>
#include <microfmt/parsing/token_stream.hpp>

using microfmt::token_kind;

// text to parse; must outlive the stream. The rules say how to tokenize it.
microfmt::token_stream ts{"set x = 42", microfmt::rules::config_kv};

ts.peek();   // next token, NOT consumed
ts.peek2();  // the token after it (two-token lookahead)
ts.next();   // consume and return the next token
ts.at_end(); // true when only `end` is left

// check: test without consuming. Use it to pick which rule to run.
ts.check(token_kind::keyword);               // by kind
ts.check(token_kind::punctuation, "{");      // by kind and exact spelling

// accept: consume only if it matches, returns reloco::optional<token>.
// Use it for optional pieces ("an optional comma").
if (auto comma = ts.accept(token_kind::punctuation, ",")) { /* consumed; comma->text is the token */ }

// skip: same, but only returns bool. Use it when you do not need the token.
bool had_comma = ts.skip(token_kind::punctuation, ",");

// expect: consume or fail with a parse_error located at the offending token.
// Use it for mandatory pieces. The message must be a string literal.
auto colon = ts.expect(token_kind::punctuation, ":", "expected ':'");
if (!colon) {
  // colon.error().message == "expected ':'"
  // colon.error().at.line / .column point at the token that was found instead
}

// error_here: build your own error at the next token, e.g. for semantic checks.
auto err = ts.error_here("unknown command");
```

Once the input is exhausted every read yields a `token_kind::end` token, so a
loop that always consumes at least one token is guaranteed to terminate.
Malformed input (`error` tokens from the lexer) never matches any kind except
`token_kind::error`, so `expect` rejects it.

## Backtracking

`token_stream` is trivially copyable, so backtracking is just a copy:

```cpp
const microfmt::token_stream saved = ts; // remember the position
// ... try a rule; if it fails ...
ts = saved;                              // rewind and try another rule
```

## Limiting recursion

```cpp
size_t depth = 0;                        // owned by the caller, shared by all rules
reloco::expected<void, microfmt::parse_error> parse_value(microfmt::token_stream &ts, size_t &depth) {
  microfmt::depth_guard guard{depth, 64}; // counter, maximum nesting
  if (!guard) {                           // false once nesting exceeds 64
    return reloco::unexpected<microfmt::parse_error>(ts.error_here("nesting too deep"));
  }
  // ... recurse into nested values; the guard decrements `depth` on return
}
```

## Worked example: a JSON validator

See [`examples/json_parser_demo.cpp`](../examples/json_parser_demo.cpp). The
shape of a recursive-descent parser is:

```cpp
// one function per grammar rule; each returns expected<void, parse_error>
//   value := string | number | keyword | array | object
reloco::expected<void, microfmt::parse_error> parse_value(microfmt::token_stream &ts, size_t &depth) {
  if (ts.check(token_kind::punctuation, "[")) { /* parse_array(...) */ }
  if (ts.check(token_kind::punctuation, "{")) { /* parse_object(...) */ }
  if (ts.skip(token_kind::string) || ts.skip(token_kind::integer)) { return {}; }
  return reloco::unexpected<microfmt::parse_error>(ts.error_here("expected a value"));
}
```

Output of the demo:

```
ok:    {"name": "microfmt", "tags": [1, -2.5, true, null]}
error: expected a value at 1:13 in {"a": [1, 2,, 3]}
error: trailing input at 1:8 in [1, 2] extra
```

Tip: the `json` preset leaves `-` as an error byte; the demo copies the preset
and adds `-` as an operator so `-2.5` parses as `op` followed by a number.

## Expressions with operator precedence

Arithmetic-style input such as `1 + 2 * 3 - -4` needs precedence (`*` binds
tighter than `+`) and associativity (`2 ^ 3 ^ 2` groups to the right). Writing
one grammar function per level is repetitive, so `parse_expression` does it
from operator tables. It builds **no AST**: you give it callbacks that produce
whatever value type you want (a number to evaluate directly, or an index into
your own node pool).

```cpp
// Operator spellings the lexer must emit as `op` tokens (set on lexer_rules::operators).
constexpr microfmt::string_view kOps[] = {"+", "-", "*", "/", "^"};

// Higher precedence binds tighter. `^` is right associative: 2^3^2 == 2^(3^2).
constexpr microfmt::binary_op kBinary[] = {
    {"+", 10}, {"-", 10}, {"*", 20}, {"/", 20}, {"^", 40, microfmt::assoc::right}};

// Unary minus: tighter than `*`, looser than `^`, so -2^2 == -(2^2).
constexpr microfmt::prefix_op kPrefix[] = {{"-", 30}};

microfmt::lexer_rules rules;
rules.operators = kOps;
microfmt::token_stream ts{"1 + 2 * 3", rules};

// primary: parses a leaf (number, identifier, call). "(" expr ")" is handled for you.
auto primary = [](microfmt::token_stream &s) -> reloco::expected<int64_t, microfmt::parse_error> { /* ... */ };
// make_binary: combines two operand values; return a node here to build a tree instead.
auto binary = [](const microfmt::binary_op &op, int64_t l, int64_t r) -> reloco::expected<int64_t, microfmt::parse_error> { /* ... */ };
// make_prefix: applies a unary operator to its operand.
auto prefix = [](const microfmt::prefix_op &op, int64_t v) -> reloco::expected<int64_t, microfmt::parse_error> { /* ... */ };

// T is the value type; the last argument (default 64) limits nesting depth.
auto result = microfmt::parse_expression<int64_t>(ts, kBinary, kPrefix, primary, binary, prefix);
```

Operators are matched by spelling against `op` and `keyword` tokens (so `and`
or `mod` work if they are keywords). The stream is left after the expression;
check `ts.at_end()` yourself if trailing input is an error. A missing operand,
a missing `)` or nesting deeper than the limit returns a `parse_error`.
See [`examples/calc_demo.cpp`](../examples/calc_demo.cpp) for a full calculator, and
[the mini language example](mini-language.md) for a complete program that combines every helper.

### Postfix operators: calls, indexing, `x!`

`f(x)` and `a[i]` bind to the operand on their left, so they are *postfix*
operators. Pass a `postfix_op` table and a fourth callback; the stream is
positioned just after the operator spelling, so the callback parses whatever
follows (call arguments, an index) and consumes the closing bracket itself.

```cpp
// Spelling may be an op, keyword or punctuation token; higher precedence binds tighter.
// `[` is punctuation, so index operators work even though `[` is not an `op` token.
constexpr microfmt::postfix_op kPostfix[] = {{"!", 50}, {"[", 60}};

// make_postfix(op, operand, ts): `ts` is right after "!" or "[".
auto postfix = [](const microfmt::postfix_op &op, int64_t v, microfmt::token_stream &s)
    -> reloco::expected<int64_t, microfmt::parse_error> {
  if (op.text == "!") { return factorial(v); }
  // "[": parse the index (any rule you like, e.g. parse_expression again), then the closing bracket.
  /* ... */
};

// Overload with the postfix table and callback inserted after `prefix` / before the other callbacks.
auto result = microfmt::parse_expression<int64_t>(ts, kBinary, kPrefix, kPostfix, primary, binary, prefix, postfix);
```

Precedence orders postfix against prefix: with `-` at 30, `!` at 50 gives
`-a!` = `-(a!)`, while `!` at 10 gives `(-a)!`.

### The conditional operator `cond ? then : else`

`?:` has three operands, so it is configured separately. The middle operand is
a full expression, and the else part is parsed at the ternary precedence, which
makes it right associative (`a ? b : c ? d : e` is `a ? b : (c ? d : e)`).

```cpp
// Spellings of "?" and ":" (op, keyword or punctuation tokens) and the precedence.
// Both must be lexed: add them to lexer_rules::operators, or use punctuation.
microfmt::ternary_op ternary{"?", ":", 5}; // 5 = below + - * /, so a + b ? c : d is (a + b) ? c : d

// make_ternary(op, condition, then_value, else_value) builds the result value.
auto make_ternary = [](const microfmt::ternary_op &, int64_t c, int64_t t, int64_t e)
    -> reloco::expected<int64_t, microfmt::parse_error> { return c != 0 ? t : e; };

// Overload with postfix AND ternary; pass an empty postfix span if you only need ?:.
auto result = microfmt::parse_expression<int64_t>(
    ts, kBinary, kPrefix, microfmt::span<const microfmt::postfix_op>{}, ternary,
    primary, binary, prefix, postfix, make_ternary);
```

A missing `:` is a `parse_error` "expected ':'" at the offending token.

### Keyword forms (mixfix): `if c then a else b`

Why: some forms start with a keyword and contain several sub-expressions in between more keywords
(`if … then … else`, `let x = e in body`). They are not operators, and `primary` cannot parse them
because it cannot re-enter the precedence parser. A *mixfix* form is recognised by its first token,
wherever an operand is expected, and its callback parses the rest itself, calling back into the
expression parser for each sub-expression.

How: describe every operator table in one `expression_grammar` and pass one `actions` object whose
member functions are the callbacks.

```cpp
// "if" is matched wherever an operand is expected; 5 is a hint the callback may use for the last part.
constexpr microfmt::mixfix_op kForms[] = {{"if", 5}};

struct actions {
  // Required: leaves (numbers, names).
  reloco::expected<int64_t, microfmt::parse_error> primary(microfmt::token_stream &ts);
  // Required when the binary / prefix tables are non-empty. Same shapes as the other overloads.
  reloco::expected<int64_t, microfmt::parse_error> make_binary(const microfmt::binary_op &, int64_t l, int64_t r);
  reloco::expected<int64_t, microfmt::parse_error> make_prefix(const microfmt::prefix_op &, int64_t v);

  // Called after "if" was consumed. `expr.parse(min_precedence)` parses one sub-expression.
  template <typename Expr>
  reloco::expected<int64_t, microfmt::parse_error> make_mixfix(const microfmt::mixfix_op &op,
                                                               microfmt::token_stream &ts, Expr &expr) {
    auto cond = expr.parse(0);                                  // the condition: a full expression
    if (!cond) return cond;
    if (auto kw = ts.expect(microfmt::token_kind::identifier, "then", "expected 'then'"); !kw)
      return reloco::unexpected<microfmt::parse_error>(kw.error());
    auto a = expr.parse(0);                                     // the "then" branch
    if (!a) return a;
    if (auto kw = ts.expect(microfmt::token_kind::identifier, "else", "expected 'else'"); !kw)
      return reloco::unexpected<microfmt::parse_error>(kw.error());
    auto b = expr.parse(op.precedence);                         // the "else" branch: extends as far as op.precedence allows
    if (!b) return b;
    return cond.value() != 0 ? a.value() : b.value();
  }
};

// grammar: binary, prefix, postfix, mixfix, optional ternary (empty members are simply unused).
const microfmt::expression_grammar grammar{kBinary, kPrefix, {}, kForms, {}};
actions a;
auto result = microfmt::parse_expression<int64_t>(ts, grammar, a);  // max_depth defaults to 64
```

Notes:

* `make_postfix`, `make_ternary` and `make_mixfix` may be left out of `actions`; using the matching
  grammar part then fails with a `parse_error` ("no make_mixfix callback", …) instead of not compiling.
* A mixfix keyword wins over `primary`, so `if` can no longer be a variable name; other identifiers
  (`iffy`) still reach `primary`.
* Mixfix forms are only looked for in operand position, so they never clash with binary or postfix
  operators. Nested forms count towards `max_depth` (`nesting too deep` as usual).
* The `precedence` of a `mixfix_op` is only a hint passed back to your callback: with a low value the
  `else` branch swallows `+ 1` in `if a then b else c + 1`; with a high one the `+ 1` applies to the whole `if`.
* This API needs no AST: `T` can be a number, a node index, anything movable.

## Delimited lists

Arrays, call arguments and objects repeat the same loop (open bracket,
optional empty list, items separated by commas, close bracket). `parse_list`
does it; you only describe one element:

```cpp
microfmt::list_options opt;   // defaults: "[" ... "]" separated by ","
opt.open = "(";               // bracket spellings are punctuation tokens
opt.close = ")";
opt.allow_trailing = true;    // accept (1, 2,); default is false

// item(ts) must consume exactly one element and return expected<void, parse_error>.
auto r = microfmt::parse_list(ts, opt, [](microfmt::token_stream &s)
    -> reloco::expected<void, microfmt::parse_error> {
  auto n = s.expect(microfmt::token_kind::integer, "expected a number");
  if (!n) { return reloco::unexpected<microfmt::parse_error>(n.error()); }
  return {};
});
// Errors: "expected opening bracket", or "expected separator or closing bracket" at the offending token.
```

Set `open = ""` for a bare list, and `close = ""` when the list simply ends
after the last item. The loop only continues after consuming a separator, so
it always terminates. See `examples/json_parser_demo.cpp`.

## Alternatives and optional parts

Grammars often say "this is an A, or a B, or a C". Trying them by hand means
saving the stream and restoring it on every failure path; forgetting one leaves
the stream half-consumed. Three helpers do it for you. Every rule is a callable
`expected<T, parse_error>(token_stream &)`.

```cpp
// attempt: run one rule; if it fails the stream is put back as it was.
auto decl = microfmt::attempt(ts, parse_function);

// parse_choice: try each rule from the SAME position; the first success wins.
// All rules must return the same T. If all fail, the stream is restored and you get
// the error of the alternative that got furthest into the input, because that is
// almost always the one the user meant (ties keep the earlier alternative).
auto item = microfmt::parse_choice(ts, parse_function, parse_variable, parse_alias);

// parse_optional: value or "nothing here". It yields an empty reloco::optional only if the
// rule failed WITHOUT consuming input; a failure after consuming (an `else` with no
// body) stays an error, so real mistakes are not silently hidden.
auto maybe_else = microfmt::parse_optional(ts, parse_else_branch); // expected<optional<T>, parse_error>
```

Alternatives are tried in order, so put the more specific rule first. See
[`examples/config_check_demo.cpp`](../examples/config_check_demo.cpp).

## Repetition without separators

`parse_list` needs separators. For "zero or more things" (statements in a block,
digit groups) use `parse_repeat` or `parse_until`. They hand each parsed value to a
`sink` callback, so *you* choose where values go (a fixed array plus counter, or just
a count); nothing allocates. Both guarantee termination: an item that consumes no
input can never spin the loop.

```cpp
// parse_repeat: keep parsing `item` while it succeeds. It stops quietly when an item fails
// WITHOUT consuming input (that is just "no more items"); a failure AFTER consuming input
// is a real mistake and is returned as an error.
// min: fewest items allowed (error `too_few_message` otherwise); max: stop after this many.
size_t total = 0;
auto count = microfmt::parse_repeat(
    ts, {.min = 1, .max = 16, .too_few_message = "expected a number"},
    parse_number,                       // expected<int, parse_error>(token_stream &)
    [&](int &&value) { total += size_t(value); }); // sink: called once per item, in order

// parse_until: items up to a terminator, which is left unconsumed. Use it for blocks:
// every item failure is an error because the closing token has not been seen yet, and
// reaching the end of input first reports "unexpected end of input".
auto body = microfmt::parse_until(ts, microfmt::token_kind::punctuation, "}", parse_item, sink);
(void)ts.expect(microfmt::token_kind::punctuation, "}", "expected '}'"); // you consume the closer
```

See `block` in [`examples/config_check_demo.cpp`](../examples/config_check_demo.cpp).

## Reporting several errors

By default the first error aborts. To report more in one run, skip ahead to a
known-good point and carry on. `parse_statements` does the whole loop: it runs
your statement rule until the input ends; on an error it stores the error,
skips to the next *sync point* and continues. `error_list<N>` keeps the errors
in fixed inline storage (no heap); errors beyond `N` are only counted, so
hostile input cannot grow memory.

```cpp
// Statements end with ';', so ';' is where we can resume after an error.
// An empty `text` in a token_sync matches any spelling of that kind.
constexpr microfmt::token_sync kSemicolon[] = {{microfmt::token_kind::punctuation, ";"}};

// statement(ts) parses ONE statement and returns expected<void, parse_error>.
auto statement = [](microfmt::token_stream &s) -> reloco::expected<void, microfmt::parse_error> { /* ... */ };

microfmt::error_list<8> errors;   // room for 8 errors; the rest are counted
size_t good = microfmt::parse_statements(ts, kSemicolon, errors, statement);

const auto stored = errors.errors();          // span of the stored errors
// errors.total()      all errors found, errors.overflowed()  true if some were not stored
// Show each one with the caret formatter (see "Showing errors").
```

After an error the sync token is consumed too. If no sync token remains before
the end of input, parsing stops. A statement that succeeds without consuming
anything is skipped by one token, so the loop always terminates. Stored errors
borrow the parsed text, so the list must not outlive it. See
[`examples/config_check_demo.cpp`](../examples/config_check_demo.cpp).

For a custom loop use `recover_to(ts, sync)` directly: it consumes tokens until
the next one matches a sync point (left unconsumed) or the input ends, and
returns how many it skipped.

### Blocks: balanced recovery

`recover_to` stops at the first sync token, even one inside a nested `{ ... }`.
After a broken statement header such as `server x { a = 1; b = 2; }`, recovery
would resume in the middle of the block and report bogus follow-up errors for
`b = 2;` and the stray `}`. `recover_balanced` treats bracket pairs as one unit
and honours sync tokens only outside them.

```cpp
// Which tokens open and close a nested region. Matching is by kind and exact spelling.
constexpr microfmt::bracket_pair kBraces[] = {
    {microfmt::token_kind::punctuation, "{", microfmt::token_kind::punctuation, "}"}};

// Drop-in overload of parse_statements that recovers with recover_balanced.
size_t good = microfmt::parse_statements(ts, kSemicolon, kBraces, errors, statement);

// Or in a hand-written loop; the optional 4th argument caps nesting (default 64):
// beyond it recovery stops at the current token, so hostile input costs linear time.
(void)microfmt::recover_balanced(ts, kSemicolon, kBraces);
```

A closing bracket with no opener is skipped unless it is itself a sync token. An
unmatched opener simply runs to the end of input.

### Recovering inside a block

`recover_balanced` helps when the error is *before* a block. If the error is
*inside* `net { port = ; host = local; }`, the block rule has already consumed
`{`, so the outer loop would resume mid-block and trip over the closing `}`. The
block rule must keep parsing its own body. `parse_statements_until` does that:
it runs your statement rule until the closing token (left unconsumed), recovers
after each error without ever swallowing that closer, and records every error in
the list you share with the outer loop.

```cpp
// The block rule needs the shared error list, so make it a small function object.
struct block_rule {
  microfmt::error_list<8> &errors;
  reloco::expected<void, microfmt::parse_error> operator()(microfmt::token_stream &ts) const {
    // ... expect the block name and '{' ...
    // Parse the body: statements until '}'. Sync tokens and brackets work as in parse_statements;
    // the closer need not be listed in `sync`. Body errors go into `errors`.
    (void)microfmt::parse_statements_until(ts, microfmt::token_kind::punctuation, "}",
                                           kSemicolon, kBraces, errors, setting);
    // ... expect the closing '}' ...
    return {};  // the block counts as parsed; its problems are already in `errors`
  }
};
```

A body error does not fail the block, so the outer loop does not report it a
second time; compare `errors.total()` before and after if you need to know
whether the block was clean. If the input ends before the closer, one
"unexpected end of input" error is recorded (and a block rule that then expects
the closer reports its own). Nest blocks by recursion, and keep `depth_guard` in
the block rule. See [`examples/config_check_demo.cpp`](../examples/config_check_demo.cpp).

### Several accepted kinds

`expect_one_of` consumes one token of any listed kind and otherwise fails with
your message, so errors can say what was really allowed.

```cpp
constexpr microfmt::token_kind kValue[] = {
    microfmt::token_kind::integer, microfmt::token_kind::string, microfmt::token_kind::identifier};
// The message is a literal (errors borrow static text; nothing allocates).
auto value = microfmt::expect_one_of(ts, kValue, "expected an integer, string or identifier");
```

## Showing errors

`<microfmt/parsing/token_stream_formatters.hpp>` renders a `parse_error` with
the offending source line and a caret, so you do not rebuild that by hand:

```cpp
#include <microfmt/parsing/token_stream_formatters.hpp>

// report(error, source): `source` must be the exact text that was parsed.
microfmt::format_to(sink, "{}", microfmt::report(result.error(), source));
// expected a number (1:9)
// 4 * (3 +
//         ^
```

Only the failing line is shown (CRLF is stripped, tabs are preserved so the
caret lines up) and an out-of-range offset is clamped, so it never reads out of
bounds.

## Notes on errors

Some mistakes are noticed far from their cause: a missing `}` is only found at the
end of the input, but the useful fact is where the block was opened. Attach that
location with `with_note`; `report()` prints it as a second snippet.

```cpp
// Keep the token of the opener when you consume it.
auto open = ts.expect(microfmt::token_kind::punctuation, "{", "expected '{'");
// ... parse the body ...
if (auto close = ts.expect(microfmt::token_kind::punctuation, "}", "expected '}'"); !close) {
  // with_note returns a copy of the error: (error, note text, token the note points at).
  // The note text must be a literal (static storage). One note per error.
  return reloco::unexpected<microfmt::parse_error>(
      microfmt::with_note(close.error(), "block opened here", open.value()));
}
```

```text
expected '}' (9:1)

^
note: block opened here (8:6)
last { z = 1;
     ^
```

Errors without a note print exactly as before, and `error_list` stores notes
along with the errors, so nothing else changes.

## Limits

* A single parse aborts on its first error; use `parse_statements` for multi-error runs (see "Reporting several errors").
* Expressions support binary infix, prefix, postfix, `?:` and parenthesised groups; parse other mixfix syntax in your own `primary` callback.
* Same limits as the lexer: bytes only, one contiguous buffer.
