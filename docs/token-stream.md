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

## Limits

* No error recovery: the first error aborts (use backtracking to retry).
* No precedence-climbing helper; write one rule per precedence level.
* Same limits as the lexer: bytes only, one contiguous buffer.
