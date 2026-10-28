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

## Source spans

Why: a parse that only returns a value cannot say *where* the value came from. Anything that keeps a
node around (an AST, a symbol table, a later "unknown key" error) needs the source range of what the
rule consumed. The stream now remembers the last consumed token (`previous()`) and the whole text
(`source()`), which is all that `span_from` needs.

```cpp
// Remember where the rule starts, run it, then ask for the range it consumed.
const microfmt::token first = ts.peek();
auto value = my_rule(ts);
microfmt::source_span where = microfmt::span_from(ts, first);
// where.text   -> exact source text, e.g. "config.server.port" (no surrounding whitespace/comments)
// where.offset -> byte offset of the first token; where.line / where.column -> its 1-based position

// Shortcut: parse_spanned runs the rule and pairs its value with the span; errors pass through as-is.
auto path = microfmt::parse_spanned(ts, my_rule);   // expected<spanned<T>, parse_error>
// path.value().value  is the rule's result, path.value().span is the source_span.
```

Nothing consumed gives an empty span at `first`. Backtracking (`attempt`, `parse_choice`) restores
`previous()` together with the stream, so spans stay correct after a failed alternative.

## Chains: `item (separator item)*`

Why: paths (`a.b.c`), unions (`x | y`) and scopes (`f::g`) are a loop, not a precedence problem.
`parse_list` drops the separators and `parse_expression` needs operator tables for what is a plain loop.

```cpp
// item(ts)            parses one segment, e.g. a name
// fold(sep, l, r)     combines the chain so far (l) with the new item (r); `sep` is the separator token
// "." is matched by kind + spelling; 4 is the maximum number of items (default: unlimited).
auto value = microfmt::parse_chain<T>(ts, microfmt::token_kind::punctuation, ".", item, fold, 4);
```

The fold is left to right: `10 - 3 - 2` with a subtracting fold is `(10 - 3) - 2`. A separator that
is not followed by an item is an error at that token (the separator is consumed; there is no silent
fall-back to a shorter chain), and exceeding the limit fails with "too many items in chain". See
[`examples/spans_demo.cpp`](../examples/spans_demo.cpp) for both helpers together.

## Left recursion: `A := A suffix | seed`

Why: a rule that starts with itself (`access := access "." name | name`) would call itself forever in
recursive descent. The fix is always the same loop: parse the seed, then keep extending the value while a
suffix matches. `parse_chain` handles one repeated separator and item and `postfix_op` needs precedence
tables; `parse_left_recursive` is for suffixes of different shapes (`a.b(1)[2]`) decided by looking at the
next token.

```cpp
// seed(ts)           parses the leftmost operand, e.g. a name.
// extend(ts, left)   looks at the next token: if a suffix starts there, consume it, replace `left` with the
//                    extended value and return true; otherwise return false and consume nothing.
//                    A malformed suffix returns its error, which is passed on unchanged.
auto extend = [](microfmt::token_stream &s, Node &left) -> reloco::expected<bool, microfmt::parse_error> {
  if (s.skip(microfmt::token_kind::punctuation, ".")) {
    auto name = s.expect(microfmt::token_kind::identifier, "expected a member name");
    if (!name) return reloco::unexpected<microfmt::parse_error>(name.error());
    left = make_member(std::move(left), name.value());   // move: `left` may own a subtree
    return true;
  }
  return false;                                           // no suffix here: the loop ends
};
// 8 = maximum number of suffixes (default: unlimited); more fails with "too many suffixes".
auto node = microfmt::parse_left_recursive<Node>(ts, seed, extend, 8);
```

The loop is iterative, so long chains cannot overflow the stack. An `extend` that returns `true` without
consuming a token fails with "left recursion made no progress" instead of hanging. See
[`examples/left_recursion_demo.cpp`](../examples/left_recursion_demo.cpp).

## Avoiding repeated work (memoization)

Why: `parse_choice` and `attempt` re-run a rule after a failed alternative. With nested alternatives that
share a prefix, such as `A := "(" A ")" "!" | "(" A ")" "?" | "x"`, the same inner `A` is parsed again at the
same position for every alternative, so the work doubles per nesting level (17 levels: 262143 rule runs).
`memoize` remembers "rule R at position P gave this result and ended here", so the repeat is a lookup (18 runs).

```cpp
// One table per result type. 64 slots, no heap. A collision just evicts the older entry: it costs a
// re-parse, never a wrong answer. Use clear() before parsing another input.
microfmt::memo_table<int, 64> memo;

reloco::expected<int, microfmt::parse_error> group(microfmt::token_stream &ts) {
  // 1 = id of this rule in the table (unique per memoized rule that shares it).
  // On a hit the stream is restored to where the rule ended and the stored result (value OR error) is returned.
  return microfmt::memoize(ts, memo, 1, [](microfmt::token_stream &s) { return group_body(s); });
}
// memo.hits() / memo.misses() show how well the cache works and help to size the table.
```

Rules for memoized rules:

* The result must depend only on the rule and the input position. A rule that reads or changes outside state
  (variables, counters, an `error_list`, evaluation while parsing) must not be memoized, because a hit skips
  those effects. The mini language evaluates while parsing and is therefore not memoized.
* The result type must be copyable (use an index into your own node pool for trees).
* Entries are keyed by offset only, not by the text: call `clear()` between inputs.

See [`examples/packrat_demo.cpp`](../examples/packrat_demo.cpp), which prints the rule runs with and without the cache.

## Lexer modes (context-dependent tokens)

Why: one `lexer_rules` value describes the whole language, but some languages
change their lexical rules by context. In `"total: ${a + 1} items"` the text
between the quotes is raw, while `${ ... }` is ordinary code again. Under the
normal rules the lexer returns the whole string as one token, so the parser
never sees the `${`. Only the parser knows which context it is in, so it
chooses the mode. `token_stream` caches two lookahead tokens that were lexed
under the *old* rules; the helpers below re-tokenize them for you.

```cpp
// Outside the string `"` opens a normal string; inside, it is plain punctuation so the parser drives the string.
const microfmt::lexer_rules code_rules;
microfmt::lexer_rules string_rules;
string_rules.quotes = "";            // no string literals in this mode
string_rules.punctuation = "\"${}";   // the bytes the template grammar needs as tokens

microfmt::token_stream ts{input, code_rules};

// with_rules: switch to string_rules, run the rule, switch back (also when the rule fails).
auto result = microfmt::with_rules(ts, string_rules, [](microfmt::token_stream &s) {
  // take_raw_until: the text after the last consumed token (spaces included) up to a `"` or `$`.
  // The result is a `string` token without quotes; it may be empty.
  const microfmt::token raw = s.take_raw_until("\"$");
  // ... then expect `$`, `{`, parse the expression with normal tokens, expect `}`, and repeat.
  return parse_template_rest(s, raw);
});
```

* `ts.set_rules(rules)` and `ts.rules()` are the building blocks if you prefer to switch by hand.
* Nesting works, and backtracking (`attempt`, `parse_choice`) restores the rules because the stream is copied.
* A switch costs a re-lex of two tokens; use it per region, not per token.
* `memoize` is keyed by offset only, so do not memoize a rule that can run under different rules at the same offset.
* The rules are copied into the stream, but spans inside them (keywords, operators) must outlive it.

See [`examples/lexer_modes_demo.cpp`](../examples/lexer_modes_demo.cpp) for a complete interpolated-string parser.

## Building a tree (node pool)

Why: parse results are often trees (`1 + 2 * 3` is a `+` node whose right child is a `*` node). Without
a heap there is no good place for children: raw pointers are unsafe, `unique_ptr` allocates and recurses when
destroyed, and a node cannot hold itself by value. `node_pool` stores every node in one vector and links them
by **index** (`node_id`), so a tree is a plain copyable value. It also solves backtracking: a failed
alternative must not leave half-built nodes behind, and the pool can simply be cut back (`rollback`).
Include `<microfmt/parsing/ast_pool.hpp>`.

```cpp
// One struct for every kind of node; children are handles (4 bytes), not pointers.
struct node {
  int64_t value{0};                       // number nodes
  char op{0};                             // 0 = number, otherwise '+' or '*'
  microfmt::token at{};                   // where to report errors about this node
  microfmt::node_id<node> left{}, right{}; // children; node_id<node>::invalid() means "none"
};

// 16 nodes live inside the pool object; more spill to the heap. 1000 = hard limit (hostile input).
microfmt::node_pool<node, 16> pool{1000};

// make() stores a node and returns its handle, or the parse_error "node pool full" located at `at`.
auto leaf = pool.make(tok, node{42, 0, tok, {}, {}});

// get() returns a checked reference to the node (terminates on a bad handle); try_get() returns an
// empty pointer instead. Both need an lvalue pool: pool().get(id) on a temporary does not compile.
auto ref = pool.get(leaf.value());

// Backtracking: pooled() wraps a rule so that when it fails the pool goes back to its earlier size.
// It composes with attempt, parse_choice and parse_optional.
auto a = microfmt::pooled(pool, [&](microfmt::token_stream &s) { return rule_a(s, pool); });
auto b = microfmt::pooled(pool, [&](microfmt::token_stream &s) { return rule_b(s, pool); });
auto result = microfmt::parse_choice(ts, a, b);

// For a tree that is only needed for one statement: mark, parse, use, rollback.
const microfmt::pool_mark before = pool.mark();
/* parse + evaluate */
pool.rollback(before);   // the next statement starts with an empty pool
```

Rules:

* Use **either** `pooled` rollback **or** `memoize` for a given pool. A rolled-back index can be reused by a
  different node, so a memoized handle could point to the wrong one. `memoize` with no rollback is fine: dead
  nodes stay until `clear()` and only cost capacity.
* A handle made after `mark()` is stale after `rollback(mark)`; do not keep it.
* Nodes hold handles, not token text; for names and literals keep a `string_id` (next section), which also
  survives `discard_consumed()` (streaming).

The [mini language](mini-language.md) is built this way: it parses each expression into a pool, resolves names
while parsing, evaluates the tree and rolls the pool back. See also
[`examples/node_pool_demo.cpp`](../examples/node_pool_demo.cpp).

## Strings in a tree (string pool)

Why: a token's `text` is a view into the source buffer. That is cheap, but a node that keeps it breaks when the
source goes away (streaming input is discarded as it is consumed), it cannot describe text that was *built*
(a string literal after its escapes are decoded), and every comparison of two names compares bytes.
`string_pool` is an interner: it copies each distinct string into one arena once and gives back a 4-byte
`string_id`. Equal text always gives an equal id, so nodes stay small and copyable, names compare as integers,
and the text lives as long as the pool. Lookup is a hash table over index arrays (no allocation per string).
It is case sensitive and meant for identifiers and literals, not for error snippets (errors keep using the
token's offset, line and column). Include `<microfmt/parsing/string_pool.hpp>`.

```cpp
// 256 bytes of text live inside the pool object; more spill to the heap. The two limits (total bytes, number of
// distinct strings) bound hostile input: past them intern() returns the parse_error "string pool full".
microfmt::string_pool<256> strings{64 * 1024, 4096};

// intern() stores the text if it is new and returns its id; `at` is where an error would be reported.
auto a = strings.intern(name_token);         // same as intern(name_token.text, name_token)
auto b = strings.intern(other_name_token);
if (a && b && a.value() == b.value()) { /* same spelling, no byte comparison needed */ }

// view() gives the text back. Keep the id, not the view: the view is invalid after the pool grows or rolls back.
reloco::string_view text = strings.view(a.value());

// find() looks a name up without adding it (a mistyped name must not grow the pool); invalid() if unknown.
microfmt::string_id known = strings.find(identifier_token.text);

// Keywords: intern them first and they get the ids 0..k-1, so "is it a keyword?" is `id.index < k`.
const reloco::string_view words[] = {"let", "print", "if"};
auto ok = strings.intern_all(reloco::span<const reloco::string_view>(words), at);
```

**How lookup works (hashing).** Comparing a new string against every stored one would be slow, so the pool
uses a hash table:

1. `intern("count")` computes a *hash*: a 32-bit number made from the bytes (FNV-1a: start from a constant, then
   for each byte XOR it in and multiply by a prime). Equal text always gives the same number; different text
   usually gives a different one.
2. The low bits of the hash choose a *bucket* (the table size is a power of two, so this is `hash & (buckets - 1)`).
   A bucket holds the index of the newest string that landed there; each stored string remembers the index of the
   previous one in the same bucket (a short chain, all in plain index arrays, no per-string allocation).
3. The pool walks that chain. For each candidate it compares the stored hash and length first (cheap) and the
   bytes last. A match returns the existing id; the end of the chain means the string is new, so its bytes are
   appended to the arena and it becomes the head of the chain.
4. When there are more strings than buckets the table doubles and every string is relinked, so chains stay
   about one entry long and a lookup costs one pass over the bytes plus a few comparisons. If memory for the
   bigger table is not available the pool keeps working with longer chains.

Two strings with the same hash (a *collision*) are fine: the byte comparison tells them apart. The hash is not
cryptographic and is not meant to resist an attacker; the `max_strings` limit bounds the work hostile input can
cause. A prefix tree (trie) would only help for prefix queries, and costs more memory and pointer chasing, so
it is not used. `rollback` is cheap for the same reason: strings are removed newest first, each is the head of
its chain, so unlinking it is one assignment.

**Building a string piece by piece.** A literal with escapes has no source range to point at. `begin()` returns
a builder that writes straight into the pool, so there is no temporary buffer:

```cpp
auto builder = strings.begin();      // only one builder can be open at a time
builder.append("line");              // append() and push_back() never fail loudly...
builder.push_back('\n');             // ...a limit hit is remembered and reported by finish()
auto id = builder.finish(literal);   // the id; if an equal string exists it is reused and the copy dropped
// Returning early (or the builder going out of scope) abandons the text: nothing is left in the pool.
```

While a builder is open, `intern` fails with "string pool busy" and `rollback`/`clear` do nothing. Do not append
a view that came from `view(id)`.

**Backtracking.** `mark()`/`rollback()` and `pooled(strings, rule)` work like the node pool: a failed
alternative gives its strings back. Leftover strings are harmless (they only use capacity), so rollback is an
optimisation for long-running parsers. To roll several pools back together, nest `pooled` calls. As with the
node pool, do not combine rollback with `memoize` on ids of the same pool.

**Streaming.** Intern before `discard_consumed()` and keep `string_id`s in nodes instead of tokens or views;
the string pool is the one structure that must survive a discard. A `token_stream` can be given the pool
(`token_stream ts{window.ref(), rules, strings.ref()}`) so that `expect_name`, `expect_string`, `intern` and
`store` work on the stream itself; see "Streaming input" below.

The [mini language](mini-language.md) interns its variable names this way. See also
[`examples/string_pool_demo.cpp`](../examples/string_pool_demo.cpp), which decodes string literals with the
builder and prints them after the source buffer is freed.

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
are copied out of the source (message, offset, line, column, length), so the list may outlive it. See
[`examples/config_check_demo.cpp`](../examples/config_check_demo.cpp).

For a custom loop use `recover_to(ts, sync)` directly: it consumes tokens until
the next one matches a sync point (left unconsumed) or the input ends, and
returns how many it skipped.

### Several errors in one list

Why: with `parse_list` one bad argument in `f(1, +, 3, *)` aborts the statement, so the second
mistake is only found after the first is fixed. `parse_list_recovering` records the bad item, skips to
the next separator or the closing bracket (nested brackets are skipped as units) and keeps checking.

```cpp
// Brackets to skip as units while recovering; pass an empty span if elements have no nested brackets.
constexpr microfmt::bracket_pair kParens[] = {{microfmt::token_kind::punctuation, "(",
                                               microfmt::token_kind::punctuation, ")"}};
microfmt::error_list<16> errors;                    // receives the error of every bad item
const microfmt::list_options options{"(", ")", ",", false};

// item(ts) has the same contract as for parse_list: consume one element or return its error.
auto failed = microfmt::parse_list_recovering(ts, options, errors, kParens, item);
if (!failed) {
  return reloco::unexpected<microfmt::parse_error>(failed.error()); // no opener / input ended: report once
}
if (failed.value() != 0) {
  // The item errors are already in `errors`; return a summary so the call itself is marked unusable.
  return reloco::unexpected<microfmt::parse_error>(microfmt::parse_error{"call has invalid arguments", name});
}
```

The result is the number of failed items. Shape errors that cannot be recovered locally (missing opening
bracket, input ending before the closer) are returned as the error and are *not* added to `errors`, so they
are reported once by the caller. A non-zero count means the list is not usable: do not evaluate or build from
it, but parsing can go on after it. See the call arguments in `examples/mini_lang.hpp`.

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

## Streaming input

Why: everything above assumes the whole text sits in one buffer. A 2 GB log, a socket or a UART on a
microcontroller cannot be held in memory. `stream_window<N>` keeps only `N` bytes, refills them from a reader
function and lets the parser throw away what it has finished with. The rest of your parser does not change:
every helper still takes a `token_stream &`. Include `<microfmt/parsing/stream_window.hpp>`.

```cpp
// The reader fills the span it is given and returns the byte count; 0 means the end of the input. It may return
// fewer bytes than the span can hold (a socket often does); the stream keeps asking until it has enough.
struct file_reader {
  std::FILE *file;
  size_t operator()(reloco::span<char> out) noexcept { return std::fread(out.data(), 1, out.size(), file); }
};

file_reader reader{file};
microfmt::stream_window<4096> window{reader};     // 4096 bytes is ALL the memory the input gets; minimum is 256
microfmt::string_pool<256> pool;                  // names and decoded literals, kept across statements
const microfmt::string_pool_ref strings = pool.ref(); // type-erased handle; must outlive the stream

microfmt::token_stream ts{window.ref(), rules, strings}; // omit `strings` if you do not intern

while (!ts.at_end()) {
  auto statement = parse_statement(ts);           // any rule or helper from this guide
  // ... use `statement`, store it, or record the error ...
  ts.discard_consumed();                          // statement boundary: drop consumed text, refill the window
}
```

**What a window can and cannot do.**

* Tokens carry *absolute* offsets, lines and columns (bytes and lines since the start of the input), so
  positions stay correct after the window moves. `ts.source_base()` is the absolute offset of the window's first byte.
* A token's `text` is a view into the window. It is valid until the next `discard_consumed()`; after that, use a
  `string_id` (`intern`, `expect_name`, `expect_string`) or `store(ts, token)`, which keeps a `stored_token`
  (kind, id, position: plain data) that `view(strings, stored)` turns back into a token.
* `discard_consumed()` keeps the line of the last consumed token (so first-on-line checks such as C directives
  still work) unless that line is longer than half the window. `previous().text` is empty after a discard.
  Call it between statements only, never inside `attempt` or `parse_choice`, whose rewind needs the text.
  `end_statement(ts, memo, node_pools...)` does the discard and also clears the memo table and node pools, whose
  entries may refer to dropped text; it deliberately does not clear the string pool.
* A copy of the stream made before a discard is *stale*: once its cached lookahead is used up it reads as `end`
  rather than touching moved bytes.
* One statement plus its lookahead must fit in the window, and so must the whitespace and comments in front of the
  next token (they are skipped in place, not incrementally). If a single token does not fit, `next()` returns an
  `error` token, `window_too_small()` becomes true and the parse ends cleanly instead of reading out of bounds.
  Size the window for your longest statement, with some room: 4096 suits most line-oriented formats.
* The window must also be at least 4x the *lookahead margin*, which is derived from your lexer rules (longest
  operator, string prefix, number suffix, comment marker; 16 to 64 bytes). A token that ends within the margin of
  the end of the buffered data is re-lexed after more input arrives, so `==` split across two reads is never
  mis-lexed as `=` `=`. Limit: a Lua long bracket whose level is above about 60 (`[======...[`) can be
  mis-lexed at a window border.

**Strings.** With a pool attached, `expect_name(ts, "message")` consumes an identifier and returns its
`string_id`; `expect_string(ts, "message", decoder)` consumes a string literal and returns the id of its decoded
value. Decoders: `decode_c_escapes`, `decode_json_string` (including `\uXXXX` and surrogate pairs, written as
UTF-8) and `decode_sql_string` (`''` for a quote), or your own `void(string_sink &, string_view raw_body)`. A bad
escape is a `parse_error` at the literal and leaves nothing in the pool. Error messages are always static strings,
so an error stays valid after the source is gone.

**Errors.** `error_list` copies each error into a `stored_error` (message, plus offset, line, column and length for
the place and for the note), so it survives discards; read `errors()[i].at.line` rather than `at.text`.
`report(error, source, base)` takes `ts.source()` and `ts.source_base()`: it shows the source line while it is still
in the window and just `message (line:column)` once it has been discarded. The same works for a `stored_error`.

**Raw text.** `take_raw_until(stop)` reads up to a stop byte. If the stop byte is not in the window, the piece is
cut where the window is full: `raw_cut()` is true and you call `take_raw_until` again for the next piece (the
earlier piece is dropped first, so copy or consume it before asking). Pieces are read across refills, so a heredoc
terminator split over two reads is still found.

See [`examples/streaming_demo.cpp`](../examples/streaming_demo.cpp): a 39 KB `name = value;` file parsed through a
512-byte window fed 5 bytes at a time, peak window use 68 bytes, with two stored errors printed after their text
left the window. `window_state`, `window_ref` and `stream_window` are the pieces behind it: `window_ref` is the
non-template handle the stream stores, so the stream type does not depend on `N`.

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

## Choosing a helper

There are many helpers because grammars repeat a handful of shapes. Find the
shape you are writing in the left column; the right column is the helper that
already handles its termination, depth and error-position details for you.

| You are writing... | Use | Why this one |
|---|---|---|
| Arithmetic / operators with precedence | `parse_expression` | Handles binding power and associativity; you only supply `primary` and node builders. |
| `if c then a else b`, `x ? a : b`, `f(x)`, `a[i]`, `x!` | `parse_expression` with `expression_grammar` (mixfix, ternary, postfix) | Same loop, extra operator shapes, no hand-rolled precedence. |
| `a, b, c` inside brackets | `parse_list` | Delimiters, empty list and trailing separator are handled. |
| The same, but one bad item must not hide the rest | `parse_list_recovering` | Records each bad item and resynchronises at the next separator or the closing bracket. |
| `item (sep item)*` with no brackets (`a.b.c`, `x + y + z` by hand) | `parse_chain` | Folds results as it goes; stops cleanly when no separator follows. |
| `seed suffix*` (`a(1)(2).b`), i.e. left recursion | `parse_left_recursive` | Loops instead of recursing, and rejects a suffix that consumes nothing. |
| Zero or more items until a closing token | `parse_until` | Terminates even when the item parser consumes nothing. |
| Zero or more items until the input ends | `parse_repeat` | Same no-progress guarantee. |
| A file of statements where one bad statement should not stop the rest | `parse_statements` / `parse_statements_until` | Collects errors in an `error_list` and skips to the next statement. |
| One of several alternatives | `parse_choice` | Tries each one with backtracking, reports the error that got furthest. |
| Try something and fall back | `attempt` | Restores the stream when the attempt fails. |
| An optional part | `parse_optional` | Returns "absent" without an error when the part is not there. |
| The text and position range of a node | `parse_spanned` | Wraps any rule's result with a `source_span`. |
| "Expected a, b or c" messages | `expect_one_of` | One error that names every acceptable kind. |
| Extra context on an error ("in call to f") | `with_note` / `note` | Adds a note without changing where the error points. |
| Skip to a safe point after an error | `recover_to`, `recover_balanced` | The balanced form respects nested `()`, `[]`, `{}`. |
| Tokens that depend on context (`"${expr}"`, heredocs, embedded languages) | `with_rules`, `take_raw_until` | Switches the lexer rules for a region and re-tokenizes the lookahead. |
| A syntax tree without heap pointers; undo of failed alternatives | `node_pool`, `node_id`, `pooled` | Nodes are linked by index and the pool rolls back when an alternative fails. |
| Names and literals that outlive the source, compare as integers, or are built from pieces (escapes) | `string_pool`, `string_id`, `string_pool::begin()` | Each distinct string is stored once; nodes keep a 4-byte id. Also the safe way to keep text across `discard_consumed()`. |
| Input larger than memory (files, sockets, UARTs) | `stream_window`, `discard_consumed`, `end_statement`, `expect_name`, `expect_string` | Parses through a fixed buffer; positions stay absolute; names and literals go to the string pool. |
| A backtracking rule that gets slow on nested input | `memoize` with `memo_table` | Remembers results per position; only for rules that depend on nothing else. |
| Guarding against hostile nesting | `depth_guard` | Cheap recursion limit; the helpers above already take a `max_depth`. |

Rules of thumb: start with `parse_expression` for anything operator-shaped and
with `parse_list` for anything comma-shaped; reach for the recovering variants
only when you report several errors per run; add `memoize` last, after you have
measured repeated work.

## Limits

* A single parse aborts on its first error; use `parse_statements` for multi-error runs (see "Reporting several errors").
* Expressions support binary infix, prefix, postfix, `?:` and parenthesised groups; parse other mixfix syntax in your own `primary` callback.
* Same limits as the lexer: bytes only. The text is one contiguous buffer unless you use a `stream_window`, in which case
  a statement (with the trivia before the next token) must fit in the window; see "Streaming input".
