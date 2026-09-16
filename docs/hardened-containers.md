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
| `microfmt::array<T, N>` | Fixed-size owning array with hardened element access |
| `microfmt::string_view` | Non-owning character view with checked and non-trapping access |
| `microfmt::span<T>` | Non-owning contiguous range used by sinks and binary formatters |
| `microfmt::expected<T, E>` | Allocation-free value-or-error result |
| `microfmt::scratch_allocator` | Aligned bump allocation in caller-owned temporary storage |

These types provide familiar standard-library-style APIs while keeping the
microfmt surface available in C++17. The non-owning view types interoperate
with their standard-library equivalents when the toolchain provides them.

## Prefer hardened types in low-level code

Use the hardened microfmt type by default when writing firmware, kernel-mode
components, interrupt handlers, crash diagnostics, protocol parsers, and
other code where an unchecked access or dangling borrow is a security issue.

| Instead of | Prefer | When |
|---|---|---|
| `std::array<T, N>` | `microfmt::array<T, N>` | Fixed-size owned storage |
| `std::span<T>` | `microfmt::span<T>` | Borrowed contiguous storage |
| `std::string_view` | `microfmt::string_view` | Borrowed character data |
| `std::expected<T, E>` | `microfmt::expected<T, E>` | Allocation-free fallible results |

This is a project default, not a ban on the standard library. Keep standard
types when required by a platform API, third-party library, ABI, or generic
ecosystem interface. Convert to a hardened view at the boundary and keep the
security-sensitive implementation on microfmt types.

Dynamic owning containers such as `std::string`, `std::vector`, and
`std::map` have no direct microfmt replacement. Use them only where allocation,
failure behavior, and execution context are explicitly acceptable. Prefer
caller-owned fixed storage and `microfmt::span` in bounded or kernel-mode
paths.

```cpp
void parse_packet(std::span<const std::byte> platform_input) {
  microfmt::span<const std::byte> input = platform_input;
  // Keep checked parsing on the hardened view from this point onward.
}
```

## Allocate bounded temporary objects

Use `microfmt::scratch_allocator` when an operation needs aligned temporary
objects or an array inside caller-owned storage. Construct it from a
`span<std::byte>` or `span<char>`, then use `create<T>(...)` for a
trivially-destructible object or `allocate<T>(count)` when a remote read will
populate raw object storage.

```cpp
alignas(std::max_align_t) std::byte scratch[128]{};
microfmt::scratch_allocator allocator(scratch);

auto *state = allocator.create<decoder_state>(initial_pc);
auto *frames = allocator.allocate<uintptr_t>(8);
if (state == nullptr || frames == nullptr) {
  return decode_error::scratch_too_small;
}

decode_frames(*state, frames, 8);
```

Allocations are monotonic and never allocate from the heap. `rest()` creates
an allocator over the unused tail for a nested operation, while
`remaining_span()` exposes that tail as bytes. `reset()` reuses the complete
backing buffer after all prior temporary objects are no longer needed.

The allocator does not run destructors. `create<T>` therefore accepts only
trivially destructible types. Check every returned pointer, keep the backing
buffer alive for the full operation, and do not share one allocator between
concurrent operations.

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

`microfmt::span<T>` follows the same model:

| Tier | Examples |
|---|---|
| Checked | `operator[]`, `front()`, `back()`, `subspan()`, `first()`, `last()` |
| Non-trapping | `try_at()`, `try_front()`, `try_back()`, `try_subspan()`, `try_first()`, `try_last()` |
| Explicitly unsafe | `unsafe_at()`, `unsafe_front()`, `unsafe_back()`, `unsafe_subspan()`, `unsafe_first()`, `unsafe_last()` |

Fallible span operations return `microfmt::expected` with a `span_error`.
`as_bytes()` creates a read-only byte view without copying the represented
storage.

`microfmt::array<T, N>` provides checked `operator[]`, fallible `try_at()`,
explicit `unsafe_at()`, hardened iterators and data access, `as_span()`,
compile-time `static_subspan<Offset, Count>()`, `fill()`, `swap()`, and
allocation-free `map()`. It supports aggregate initialization, structured
bindings, and zero-length arrays. Borrowing accessors, including `get<I>()`,
are rejected on temporary arrays. Tuple traits are provided for structured
bindings; use ADL `get` rather than expecting `std::get` or `std::apply`
interoperability.

## Debug checks and unsafe operations

`MICROFMT_DEBUG_ASSERT` protects lower-level operations whose contracts are
intended to be established by nearby code. It is active in debug builds and
when `MICROFMT_DEBUG` is defined, but it is disabled by `NDEBUG` otherwise.

Only operations explicitly named `unsafe_*` use this debug-check tier.
Checked `microfmt::span<T>` indexing and subviews remain hardened in release
builds. Both `microfmt::span<T>` and `microfmt::string_view` name their fast
paths with an `unsafe_*` prefix so security-sensitive call sites remain
visible in review.

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
expression. `microfmt::span<T>` similarly rejects indexing, pointer access,
and iterator access on temporary span objects. `microfmt::string_view` also
rejects construction from a temporary owning string.

```cpp
std::string storage = load_name();
microfmt::string_view safe = storage;

// Rejected: the owning string would be destroyed immediately.
// microfmt::string_view dangling = load_name();
```

The owner must still outlive every non-owning `microfmt::string_view` or
`microfmt::span<T>`. Hardening detects API contract violations; it cannot
extend the lifetime of referenced storage.

For non-owning references to individual objects, and for guidance on compiler
annotations, see [Lifetime safety and `value_ref`](lifetime-safety.md).

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
