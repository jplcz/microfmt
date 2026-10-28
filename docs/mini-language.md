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
```

Statements are `let NAME [= expr];`, `print(expr);` and `{ statements }`. Expressions have numbers,
variables, calls (`max`, `min`, `abs`), unary `-`, `+ - * / > <`, `?:` and parentheses. The program is
evaluated while it is parsed, so there is no syntax tree to build.

## Why it is built this way

A real tool should show *every* mistake in one run, not stop at the first, and must stay safe on hostile
input (bounded memory, bounded recursion, no overflow). The example therefore uses fixed-size storage
everywhere: 16 variables, 8 call arguments, 16 stored errors, a 1 KiB output buffer, and a nesting limit of
32. Hitting a limit is reported as a normal error.

## Which helper is used where

| Need | Helper | Where |
|---|---|---|
| Read tokens one at a time | `token_stream`, `expect`, `skip`, `check` | every rule |
| Operators with precedence | `parse_expression` with `kBinary`, `kPrefix`, a `ternary_op` | `expression()` |
| Comma-separated call arguments | `parse_list` | `parse_call()` |
| "A statement is a let, a print or a block" | `parse_choice` | `parse_statement()` |
| Optional initialiser (`let x;`) | `parse_optional` | `parse_let()` |
| Several accepted token kinds | `expect_one_of` | `parse_primary()` |
| Keep going after an error | `parse_statements`, `error_list` | `run()` |
| Keep going inside a block | `parse_statements_until` | `parse_block()` |
| Skip a `{ ... }` as one unit when recovering | `bracket_pair`, `recover_balanced` (via the two above) | `kBraces` |
| Point at where a block was opened | `with_note` | `parse_block()` |
| Show the source line and a caret | `report()` | the demo |
| Stop runaway nesting | `depth_guard` | statements and calls |

## Walkthrough of the interesting parts

**Choosing between statements.** `parse_statement` calls `parse_choice(let, print, block)`. Each
alternative starts from the same position. If all fail, you get the error of the one that got *furthest*,
so `let x 5;` reports "expected ';'" (from the `let` rule) instead of a generic "expected a statement".
A rule that does not recognise its first word fails immediately with the generic message.

**Optional pieces without hiding mistakes.** In `let x;` there is no `=`. `parse_optional` turns "failed
without consuming anything" into "nothing here". In `let x = ;` the `=` is consumed and then the
expression fails, which stays an error, so the typo is not silently treated as "no initialiser".

**Expressions.** `parse_expression` handles precedence; you only supply the leaves (`parse_primary`) and
what to do with the operands (`binary`, `prefix`, `ternary` lambdas). Calls are parsed in `parse_primary`
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
