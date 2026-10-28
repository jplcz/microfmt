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
See [`examples/calc_demo.cpp`](../examples/calc_demo.cpp) for a full calculator.

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

## Limits

* No error recovery: the first error aborts (use backtracking to retry).
* Expressions support binary infix, prefix and parenthesised groups only; parse postfix, ternary or call syntax in your own `primary` callback.
* Same limits as the lexer: bytes only, one contiguous buffer.
