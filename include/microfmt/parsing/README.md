# `microfmt/parsing`

Text-scanning building blocks that are not formatters themselves.

| File | Contents |
|---|---|
| `lexer.hpp` | The lexer engine: `token`, `token_kind`, `lexer_rules`, `lexer`, plus the `sorted_words` / `keyword_table` keyword lists |
| `token_stream.hpp` | Parser building blocks on the lexer: `token_stream` (2-token lookahead, `accept`/`expect`), `parse_error`, `depth_guard`, `parse_expression` (precedence climbing with prefix/binary/postfix operators and `?:`), `parse_list`, `recover_to`, `error_list<N>` and `parse_statements` (multi-error parsing) |
| `token_stream_formatters.hpp` | `report(parse_error, source)`: formatter showing the failing line with a caret |
| `lexer_rules_presets.hpp` | Ready-made rule sets: `c`, `cpp` (`c_like` alias), `lua`, `sql`, `toml`, `ini`, `config`, `config_kv`, `json`, `shell` |
| `lexer_formatters.hpp` | Formatters for `token_kind` / `token` and the ANSI `highlight()` view |
| `number_parse.hpp` | Integer parsing: strict `parse<T>` and `from_chars`-like `parse_prefix<T>` (all integer widths, `__int128`, `reloco::fixed_int`) |
| `number_parse_float.hpp` | Opt-in floating-point parsing (`float`/`double`/`long double`) on top of `strto*` |

User guides: [`docs/lexer.md`](../../../docs/lexer.md), [`docs/token-stream.md`](../../../docs/token-stream.md), [`docs/number-parsing.md`](../../../docs/number-parsing.md).
