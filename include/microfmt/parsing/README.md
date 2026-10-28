# `microfmt/parsing`

Text-scanning building blocks that are not formatters themselves.

| File | Contents |
|---|---|
| `lexer.hpp` | The lexer engine: `token`, `token_kind`, `lexer_rules`, `lexer`, plus the `sorted_words` / `keyword_table` keyword lists |
| `lexer_rules_presets.hpp` | Ready-made rule sets: `rules::c_like`, `rules::json`, `rules::shell` |
| `lexer_formatters.hpp` | Formatters for `token_kind` / `token` and the ANSI `highlight()` view |

User guide: [`docs/lexer.md`](../../../docs/lexer.md). Design:
[`docs/design/lexer.md`](../../../docs/design/lexer.md).
