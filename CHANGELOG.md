# Changelog

## Unreleased

## v0.4.0

### Added

- Lexer engine with token formatters and a highlighter, plus lexer rules for C, C++, Lua, SQL, INI, TOML and config files.
- Parsing toolkit: number parser, token stream (with lexer modes and stream parsing), expression parsing with ternary and mixfix operators, operator tables, left recursion, packrat memoization, error recovery (lists and blocks), source spans, AST node and string pools, and a mini-language demo.
- More basic formatters, an extended logger, a sink guide, and Lua-style float support.
- Formatter cheatsheet published as a single page (Doxygen docs removed).

### Changed

- Reduced code size: equivalent argument types are collapsed, `const_char_like` is outlined, strings are compacted and `join_view` is shared.
- CI runs on `ubuntu-26.04`.

### Fixed

- Formatter bugs and reloco-tidy findings; several string pool fixes.
- GCC 15 pedantic/shadow errors and a platform-dependent `fix_type` test.
