# `microfmt/parsing`

Text-scanning building blocks that are not formatters themselves.

| File | Contents |
|---|---|
| `lexer.hpp` | The lexer engine: `token`, `token_kind`, `lexer_rules`, `lexer`, plus the `sorted_words` / `keyword_table` keyword lists |
| `lexer_rules_presets.hpp` | Ready-made rule sets: `c`, `cpp` (`c_like` alias), `lua`, `sql`, `toml`, `ini`, `config`, `config_kv`, `json`, `shell` |
| `lexer_formatters.hpp` | Formatters for `token_kind` / `token` and the ANSI `highlight()` view |

User guide: [`docs/lexer.md`](../../../docs/lexer.md). Design:
[`docs/design/lexer.md`](../../../docs/design/lexer.md).
