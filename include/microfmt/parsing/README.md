# `microfmt/parsing`

Text-scanning building blocks that are not formatters themselves.

| File | Contents |
|---|---|
| `lexer.hpp` | The lexer engine: `token`, `token_kind`, `lexer_rules`, `lexer`, plus the `sorted_words` / `keyword_table` keyword lists |
| `token_stream.hpp` | Parser building blocks on the lexer: `token_stream` (2-token lookahead, `accept`/`expect`), `parse_error`, `depth_guard`, `parse_expression` (precedence climbing with prefix/binary/postfix operators, `?:` and keyword forms like `if … then … else` via `expression_grammar` and `mixfix_op`), `parse_list`, `recover_to`, `error_list<N>` and `parse_statements` (multi-error parsing), `attempt`, `parse_choice` and `parse_optional` (backtracking alternatives), `parse_repeat` and `parse_until` (repetition into caller storage), `recover_balanced` (bracket-aware recovery), `parse_statements_until` (recovery inside blocks), `expect_one_of` and `with_note` (error notes), `source_span`, `span_from` and `parse_spanned` (source ranges), `parse_chain` (`item (sep item)*`), `parse_list_recovering` (lists that report every bad item) |
| `token_stream_formatters.hpp` | `report(parse_error, source)`: formatter showing the failing line with a caret |
| `lexer_rules_presets.hpp` | Ready-made rule sets: `c`, `cpp` (`c_like` alias), `lua`, `sql`, `toml`, `ini`, `config`, `config_kv`, `json`, `shell` |
| `lexer_formatters.hpp` | Formatters for `token_kind` / `token` and the ANSI `highlight()` view |
| `number_parse.hpp` | Integer parsing: strict `parse<T>` and `from_chars`-like `parse_prefix<T>` (all integer widths, `__int128`, `reloco::fixed_int`) |
| `number_parse_float.hpp` | Opt-in floating-point parsing (`float`/`double`/`long double`) on top of `strto*` |

User guides: [`docs/lexer.md`](../../../docs/lexer.md), [`docs/token-stream.md`](../../../docs/token-stream.md), [`docs/number-parsing.md`](../../../docs/number-parsing.md).
