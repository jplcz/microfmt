# `microfmt/parsing`

Text-scanning building blocks that are not formatters themselves.

| File | Contents |
|---|---|
| `lexer.hpp` | The lexer engine: `token`, `token_kind`, `lexer_rules`, `lexer`, plus the `sorted_words` / `keyword_table` keyword lists |
| `lexer_rules_presets.hpp` | Ready-made rule sets: `c`, `cpp` (`c_like` alias), `lua`, `sql`, `toml`, `ini`, `config`, `config_kv`, `json`, `shell` |
| `lexer_formatters.hpp` | Formatters for `token_kind` / `token` and the ANSI `highlight()` view |
| `number_parse.hpp` | Integer parsing: strict `parse<T>` and `from_chars`-like `parse_prefix<T>` (all integer widths, `__int128`, `reloco::fixed_int`) |
| `number_parse_float.hpp` | Opt-in floating-point parsing (`float`/`double`/`long double`) on top of `strto*` |

User guides: [`docs/lexer.md`](../../../docs/lexer.md), [`docs/number-parsing.md`](../../../docs/number-parsing.md).
Designs: [`docs/design/lexer.md`](../../../docs/design/lexer.md), [`docs/design/number-parsing.md`](../../../docs/design/number-parsing.md).
