# `microfmt/parsing`

Text-scanning building blocks that are not formatters themselves.

| File | Contents |
|---|---|
| `lexer.hpp` | The lexer engine: `token`, `token_kind`, `lexer_rules`, `lexer`, plus the `sorted_words` / `keyword_table` keyword lists |
| `token_stream.hpp` | Parser building blocks on the lexer: `token_stream` (2-token lookahead, `accept`/`expect`), `parse_error`, `depth_guard`, `parse_expression` (precedence climbing with prefix/binary/postfix operators, `?:` and keyword forms like `if … then … else` via `expression_grammar` and `mixfix_op`), `parse_list`, `recover_to`, `error_list<N>` and `parse_statements` (multi-error parsing), `attempt`, `parse_choice` and `parse_optional` (backtracking alternatives), `parse_repeat` and `parse_until` (repetition into caller storage), `recover_balanced` (bracket-aware recovery), `parse_statements_until` (recovery inside blocks), `expect_one_of` and `with_note` (error notes), `source_span`, `span_from` and `parse_spanned` (source ranges), `parse_chain` (`item (sep item)*`), `parse_list_recovering` (lists that report every bad item), `parse_left_recursive` (`seed suffix*`), `memo_table` and `memoize` (packrat caching for backtracking rules), `with_rules` and `take_raw_until` (lexer modes: switch rules or read raw text, e.g. string interpolation) |
| `string_pool_ref.hpp` | `string_id`, the type-erased `string_pool_ref` a `token_stream` interns into, `string_sink` and the literal decoders `decode_c_escapes`, `decode_json_string`, `decode_sql_string` |
| `string_pool.hpp` | Identifiers and literals that outlive the source: `string_pool<N>` (hash-based interner, `mark`/`rollback`), `string_id` (4-byte handle; equal text gives an equal id), `string_pool::builder` (writes decoded literals piece by piece) and `pooled` for it |
| `ast_pool.hpp` | Syntax trees without heap pointers: `node_pool<T, N>` (nodes in a `sso_vector`, `mark`/`rollback`), `node_id<T>` (4-byte typed handle) and `pooled` (rolls the pool back when a rule fails, for `attempt`/`parse_choice`) |
| `operator_table.hpp` | Operators declared at run time: `operator_table<N>` with precedence `level`s (`add_level`, `add_level_above/below`), `add_binary/prefix/postfix/mixfix`, and the `grammar()`, `spellings()` and `keywords()` views for `parse_expression` and `lexer_rules` |
| `stream_window.hpp` | Parsing input that does not fit in memory: `stream_window<N>` (fixed buffer refilled from a reader) and the non-template `window_ref` a `token_stream` takes |
| `parse_error.hpp` | `parse_error` and `with_note` (borrow the source) and `stored_error` / `source_site` (plain data that outlives it) |
| `token_stream_formatters.hpp` | `report(parse_error or stored_error, source, base)`: formatter showing the failing line with a caret |
| `lexer_rules_presets.hpp` | Ready-made rule sets: `c`, `cpp` (`c_like` alias), `lua`, `sql`, `toml`, `ini`, `config`, `config_kv`, `json`, `shell` |
| `lexer_formatters.hpp` | Formatters for `token_kind` / `token` and the ANSI `highlight()` view |
| `number_parse.hpp` | Integer parsing: strict `parse<T>` and `from_chars`-like `parse_prefix<T>` (all integer widths, `__int128`, `reloco::fixed_int`) |
| `number_parse_float.hpp` | Opt-in floating-point parsing (`float`/`double`/`long double`) on top of `strto*` |

User guides: [`docs/lexer.md`](../../../docs/lexer.md), [`docs/token-stream.md`](../../../docs/token-stream.md), [`docs/number-parsing.md`](../../../docs/number-parsing.md).
