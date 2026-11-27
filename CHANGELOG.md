# Changelog

## Unreleased

## v0.5.3

## v0.5.2

### Fixed

- DWARF CFI decoder (`dwarf_cfi_decoder`): parses the CIE and reads `.eh_frame` FDEs (pc-relative and other `DW_EH_PE_*` pointer encodings, `z` augmentation data). It also applies the CIE's `code_align`/`data_align` factors, the CIE initial instructions, `DW_CFA_restore`, and `restore_state` keeps the current location. Opcodes it cannot apply fail the FDE instead of desynchronising the stream.

## v0.5.1

### Added

- Compile-time log level `MICROFMT_ACTIVE_LEVEL` (and CMake option `JPLCZ_MICROFMT_ACTIVE_LEVEL`): `MICROFMT_LOG_*`/`MICROFMT_LOGGER_*` macros below it become no-ops, so their format strings are not compiled into the binary.

## v0.5.0

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
