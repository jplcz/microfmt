# A complete example: the mini language

The token stream helpers are described one at a time in [token-stream.md](token-stream.md). This page
shows how they fit together in one small, real program, so you can copy the structure for your own
format or language. The code lives in [`examples/mini_lang.hpp`](../examples/mini_lang.hpp) (the language),
[`examples/mini_lang_demo.cpp`](../examples/mini_lang_demo.cpp) (a runnable demo) and
[`tests/test_mini_lang.cpp`](../tests/test_mini_lang.cpp) (what it must do).

## What it parses

```text
let x = 2 + 3 * 4;            // variable with an initialiser
let y = x > 10 ? x : 0;       // comparison and the ?: operator
print(max(x, y, 7));          // calls with comma-separated arguments
{ let z = -y; print(z); }     // blocks
infixl 5 "<>" = max;           // declare an operator: precedence, associativity, function
print(1 + 2 <> 2 * 5);        // 10
```

Statements are `let NAME [= expr];`, `print(expr);`, `{ statements }` and `infixl|infixr|infix N "op" = min|max;`.
The last one adds a binary operator at run time (`infix` forbids chaining); N is a precedence from 0 to 255 relative to
the built-in `+ - * /` (10, 20) and `< >` (8), and the operator applies `min` or `max`. The built-ins live in an
`operator_table`, so the new spelling is handed to the lexer with `set_rules` right after the declaration. Expressions have numbers,
variables, calls (`max`, `min`, `abs`), unary `-`, `+ - * / > <`, `?:` and parentheses. Statements run one at a
time; each expression is parsed into a small tree (a `node_pool`), evaluated, and the nodes are given back.
Names are resolved while parsing, so an unknown variable or function is a parse-time error; only the chosen
branch of `?:` / `if` runs, and a division by zero is reported at the divisor. Variable names are interned in a
`string_pool` (at most 16 distinct names), so a lookup is `find()` plus integer comparisons and the variables do
not keep views into the source.

## Why it is built this way

A real tool should show *every* mistake in one run, not stop at the first, and must stay safe on hostile
input (bounded memory, bounded recursion, no overflow). The example therefore uses fixed-size storage
everywhere: 16 variables, 8 call arguments, 16 stored errors, a 1 KiB output buffer, 128 nodes per expression
(the first 32 inline in the interpreter), and a nesting limit of
32. Hitting a limit is reported as a normal error.

## Which helper is used where

| Need | Helper | Where |
|---|---|---|
| Read tokens one at a time | `token_stream`, `expect`, `skip`, `check` | every rule |
| Operators with precedence | `parse_expression` with an `expression_grammar` (from an `operator_table`) | `expression()` |
| `if c then a else b` | a `mixfix_op` plus `make_mixfix`, which re-enters the parser with `expr.parse` | `expression_actions::make_mixfix` |
| Comma-separated call arguments, each bad one reported | `parse_list_recovering` | `parse_call()` |
| "A statement is a let, a print or a block" | `parse_choice` | `parse_statement()` |
| Optional initialiser (`let x;`) | `parse_optional` | `parse_let()` |
| Several accepted token kinds | `expect_one_of` | `parse_primary()` |
| Keep going after an error | `parse_statements`, `error_list` | `run()` |
| Keep going inside a block | `parse_statements_until` | `parse_block()` |
| Skip a `{ ... }` as one unit when recovering | `bracket_pair`, `recover_balanced` (via the two above) | `kBraces` |
| Point at where a block was opened | `with_note` | `parse_block()` |
| Show the source line and a caret | `report()` | the demo |
| Stop runaway nesting | `depth_guard` | statements and calls |
| Keep the syntax tree without pointers, and cap its size | `node_pool`, `node_id` | `expression()`, `parse_tree()` |
| Store variable names once and compare them as integers | `string_pool`, `string_id` | `define()`, the variable lookup in the primary rule |

## Walkthrough of the interesting parts

**Choosing between statements.** `parse_statement` calls `parse_choice(let, print, block)`. Each
alternative starts from the same position. If all fail, you get the error of the one that got *furthest*,
so `let x 5;` reports "expected ';'" (from the `let` rule) instead of a generic "expected a statement".
A rule that does not recognise its first word fails immediately with the generic message.

**Optional pieces without hiding mistakes.** In `let x;` there is no `=`. `parse_optional` turns "failed
without consuming anything" into "nothing here". In `let x = ;` the `=` is consumed and then the
expression fails, which stays an error, so the typo is not silently treated as "no initialiser".

**Expressions.** `parse_expression` handles precedence; you only supply the leaves (`parse_primary`) and
what to do with the operands (the `make_binary`, `make_prefix` and `make_ternary` members of
`expression_actions`, which here store a node in the pool and return its handle instead of computing a value;
`evaluate()` later walks the tree). `if cond then a else b` is a keyword form: `make_mixfix` parses the condition and
both branches with `expr.parse`, so they may be any expression (including another `if`), and reports
`expected 'then'` / `expected 'else'` at the token where the keyword was missing. Calls are parsed in `parse_primary`
rather than as postfix operators because the callee name is needed and the left operand is already a number
by then. Arithmetic wraps instead of overflowing, and `INT64_MIN / -1` is handled, so hostile input cannot
trigger undefined behaviour.

**Errors in a block.** `parse_block` consumes `{`, then hands the body to `parse_statements_until`, which
recovers inside the block and never swallows the closing `}`. The block records its body errors in the shared
list and counts as parsed, so the outer loop does not report the same problem twice. If `}` is missing, the
error carries a note pointing at the `{`:

```text
expected '}' (6:1)

^
note: block opened here (5:1)
{ let c = 1;
^
```

**Recovery.** `kStatementEnd` says "resume at the next `;`", and `kBraces` says "a `;` inside `{ }` does not
count". Together they stop one broken statement from producing a flood of follow-up errors. Only braces are
listed as brackets: parentheses are not, because an unbalanced `print(a;` would otherwise hide every `;`
up to the end of the input.

## Trying it

```sh
ninja -C build mini_lang_demo && ./build/examples/mini_lang_demo
```

The demo prints a valid program's output, then a broken program's output followed by all of its errors.
