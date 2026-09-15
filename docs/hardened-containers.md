<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Hardened containers and views

`microfmt` provides small C++17-compatible containers and views for code that
cannot rely on exceptions, allocation, or build-mode-dependent safety. The
core types include:

| Type | Purpose |
|---|---|
| `microfmt::string_view` | Non-owning character view with checked and non-trapping access |
| `microfmt::span<T>` | Non-owning contiguous range used by sinks and binary formatters |
| `microfmt::expected<T, E>` | Allocation-free value-or-error result |

These types interoperate with newer standard-library equivalents where the
toolchain provides them, but keep the microfmt API available in C++17.

## Security is enabled by default

Unlike conventional standard-library containers, where unchecked element
access is commonly the default, microfmt makes checked behavior the default
and requires an explicit opt-out. Checked microfmt operations use
`MICROFMT_ASSERT`. These checks remain active in release builds and are not
removed merely because `NDEBUG` is defined. Invalid checked access calls the
configured assertion handler and then traps.

This makes hardening opt-out rather than opt-in: applications get checked
behavior by default and must explicitly define `MICROFMT_DISABLE_ASSERT` to
remove it. Do this only after proving that every checked precondition is
satisfied. With assertions disabled, violating a precondition is undefined
behavior; the macro is a performance and code-size tradeoff, not an error
recovery mode.

```cmake
target_compile_definitions(firmware PRIVATE MICROFMT_DISABLE_ASSERT=1)
```

The opt-out applies globally to checked microfmt operations in that target.
Prefer selecting an `unsafe_*` operation at a measured hot call site instead
of disabling hardening for the entire program.

## Choose an access tier

`microfmt::string_view` exposes three access styles:

| Tier | Examples | Behavior on invalid input |
|---|---|---|
| Checked | `operator[]`, `front()`, `back()`, `substr()` | Assertion handler, then trap |
| Non-trapping | `try_at()`, `try_front()`, `try_back()`, `try_substr()` | Returns `microfmt::expected` with `string_view_error` |
| Explicitly unsafe | `unsafe_front()`, `unsafe_back()`, `unsafe_substr()` | Debug assertion only; caller owns the precondition |

Use checked operations when invalid input indicates a programming defect. Use
`try_*` operations for data-dependent bounds or empty-input cases that the
application expects to handle.

```cpp
microfmt::string_view input = receive_field();

if (auto first = input.try_front()) {
  consume(first.value().get());
} else {
  report_empty_field();
}

if (auto payload = input.try_substr(header_size)) {
  decode(payload.value());
} else {
  report_truncated_field();
}
```

`microfmt::expected<T, E>::value()` and `error()` are also checked. Test the
result with `has_value()` or its boolean conversion before accessing the
active alternative.

## Debug checks and unsafe operations

`MICROFMT_DEBUG_ASSERT` protects lower-level operations whose contracts are
intended to be established by nearby code. It is active in debug builds and
when `MICROFMT_DEBUG` is defined, but it is disabled by `NDEBUG` otherwise.

`microfmt::span<T>` indexing and construction use this debug-check tier.
`microfmt::string_view` names the corresponding fast paths with an
`unsafe_*` prefix so security-sensitive call sites remain visible in review.

```cpp
if (!field.empty()) {
  const char first = field.unsafe_front();
  consume(first);
}
```

Do not use an unsafe operation solely to avoid handling invalid input. Keep
the precondition adjacent to the call and prefer a checked or `try_*` API at
trust boundaries.

## Dangling-reference prevention

`microfmt::expected` deletes dereference and pointer-style accessors on
temporary result objects where the returned value could dangle after the full
expression. `microfmt::string_view` also rejects construction from a temporary
owning string.

```cpp
std::string storage = load_name();
microfmt::string_view safe = storage;

// Rejected: the owning string would be destroyed immediately.
// microfmt::string_view dangling = load_name();
```

The owner must still outlive every non-owning `microfmt::string_view` or
`microfmt::span<T>`. Hardening detects API contract violations; it cannot
extend the lifetime of referenced storage.

## Configure assertion reporting

The default assertion handler writes the failed expression, source location,
and message to standard error before trapping. Replace it when diagnostics
must go to a device log, crash record, or platform-specific transport:

```cpp
void assertion_log(const char *expression, const char *file, int line,
                   const char *message) {
  write_crash_record(expression, file, line, message);
}

microfmt::set_assert_handler(assertion_log);
```

Define `MICROFMT_DISABLE_ASSERT_STDIO` to suppress the default standard-error
dependency while preserving checks and traps:

```cmake
target_compile_definitions(firmware PRIVATE MICROFMT_DISABLE_ASSERT_STDIO=1)
```

Install a custom handler before a failure can occur if the default handler is
disabled and the platform requires a persistent diagnostic record.
