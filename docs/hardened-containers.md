<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Hardened containers and views

The hardened, C++17-compatible containers and views used throughout
microfmt — `array<T, N>`, `string_view`, `span<T>`, and `expected<T, E>` —
are provided by [`jplcz_reloco`](https://github.com/jplcz/reloco), a
dependency of microfmt, and re-exported under `microfmt::`. See reloco's
[Hardened containers and views](https://github.com/jplcz/reloco/blob/master/docs/hardened-containers.md)
guide for the full type table, the checked/`try_*`/`unsafe_*` access-tier
model, and dangling-reference prevention.

microfmt additionally provides `microfmt::scratch_allocator`, a bump
allocator over caller-owned storage, for code that cannot rely on
exceptions or dynamic allocation.

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
access is commonly the default, microfmt and reloco make checked behavior the
default and require an explicit opt-out. Checked operations use
`RELOCO_ASSERT`. These checks remain active in release builds and are not
removed merely because `NDEBUG` is defined. Invalid checked access calls the
configured assertion handler and then traps.

This makes hardening opt-out rather than opt-in: applications get checked
behavior by default and must explicitly define `RELOCO_DISABLE_ASSERT` to
remove it. Do this only after proving that every checked precondition is
satisfied. With assertions disabled, violating a precondition is undefined
behavior; the macro is a performance and code-size tradeoff, not an error
recovery mode.

```cmake
target_compile_definitions(firmware PRIVATE RELOCO_DISABLE_ASSERT=1)
```

The opt-out applies globally to checked microfmt operations in that target.
Prefer selecting an `unsafe_*` operation at a measured hot call site instead
of disabling hardening for the entire program.

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

Define `RELOCO_DISABLE_ASSERT_STDIO` to suppress the default standard-error
dependency while preserving checks and traps:

```cmake
target_compile_definitions(firmware PRIVATE RELOCO_DISABLE_ASSERT_STDIO=1)
```

Install a custom handler before a failure can occur if the default handler is
disabled and the platform requires a persistent diagnostic record.
