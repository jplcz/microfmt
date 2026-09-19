<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Lifetime safety, `value_ref`, and `value_ptr`

microfmt combines type-level borrowing rules with optional compiler
annotations. The type system provides portable enforcement, while annotations
give supporting compilers additional information for diagnostics and static
analysis.

## Borrow persistent values with `value_ref`

`microfmt::value_ref<T>` is a small, non-null reference wrapper for values
stored in type-erased metadata, registries, tasks, and other non-owning
structures. It accepts compatible lvalues and rejects rvalues:

```cpp
#include <microfmt/value_ref.hpp>

task_metadata metadata{42};
microfmt::value_ref ref(metadata);

consume(*ref);
consume(ref->id);

// Rejected: the temporary would be destroyed at the semicolon.
// microfmt::value_ref dangling(task_metadata{42});
```

The wrapper stores only a pointer and does not extend the referenced object's
lifetime. The owner must outlive the `value_ref` and every copy of it.
`value_ref<T>` preserves mutable access to a mutable lvalue;
`value_ref<const T>` provides an explicitly read-only borrow:

```cpp
device state{};
microfmt::value_ref<device> mutable_state(state);
mutable_state->reset();

microfmt::value_ref<const device> observed_state(state);
inspect(*observed_state);
```

Class-template argument deduction preserves the lvalue's type, including
`const`. Explicit `value_ref<Base>` construction also accepts an lvalue of a
publicly derived type. Unrelated types and temporary values are rejected at
compile time.

The inspector's `concrete_metadata_map` uses `value_ref` to store
heterogeneous borrowed properties. See
[Inspector: metadata maps](inspector/metadata.md) for storage, iteration, and
formatting lifetime requirements.

`value_ref` is part of the main library interface rather than the inspector
subdirectory. Read-only adapters such as `hash_view`, `variant_view`, and the
`{fmt}` compatibility output iterator store `value_ref<const T>` explicitly.

## Store nullable borrows with `value_ptr`

`microfmt::value_ptr<T>` is a nullable, non-owning pointer wrapper. It retains
ordinary pointer size and trivial-copy behavior while marking the type as
`MICROFMT_POINTER` and marking raw-pointer construction and borrowed accessors
with `MICROFMT_LIFETIMEBOUND`.

```cpp
device state{};
microfmt::value_ptr<device> ptr(&state);

if (ptr) {
  ptr->reset();
}
```

Use `value_ref` when null is not a valid state and read-only access is enough.
Use `value_ptr` for optional references, mutable pointees, type-erased context
storage, and objects that must be rebound or default-constructed before a
context is available. Neither wrapper owns or extends the lifetime of the
pointed object.

The library uses `value_ptr` for retained type-erased inspector contexts,
symbol-resolution contexts, callback and container targets, metadata value
pointers, and PMR resources. Owning allocation links, static virtual tables,
and public C-style callback ABI fields remain raw pointers.

## Lifetime annotations

`microfmt/lifetime.hpp` defines portable annotation macros:

| Macro | Meaning |
|---|---|
| `MICROFMT_LIFETIMEBOUND` | A returned or stored borrow cannot outlive the annotated source |
| `MICROFMT_OWNER` | The annotated type owns the storage reached through it |
| `MICROFMT_POINTER` | The annotated type is a non-owning pointer-like wrapper |
| `MICROFMT_UNSAFE_BUFFER_USAGE` | Marks a low-level function using intentionally unchecked buffer operations |
| `MICROFMT_LIFETIME_CAPTURE_BY(...)` | Declares that one or more named parameters retain a borrow |
| `MICROFMT_LIFETIME_CAPTURE_BY_THIS` | Declares that an object retains a constructor or member-function argument |
| `MICROFMT_NONNULL(...)` | Declares pointer parameters that must not be null |
| `MICROFMT_ATTR_ACCESS(...)` | Describes pointer access direction to GCC and Clang |
| `MICROFMT_ATTR_ACCESS_SIZE(...)` | Associates pointer access with a size parameter |
| `MICROFMT_NONSTRING` | Marks a character array as raw storage rather than a NUL-terminated string |
| `MICROFMT_MALLOC_PAIR(...)` | Associates a GCC allocator with its matching deallocator |
| `MICROFMT_ASSUME_ALIGNED(...)` | States the minimum alignment of a returned pointer |
| `MICROFMT_DIAGNOSE_IF(...)` | Adds a Clang call-site diagnostic for an invalid argument condition |
| `MICROFMT_MUSTTAIL` | Requires a Clang tail call when placed on a return statement |
| `MICROFMT_COLD`, `MICROFMT_HOT` | Marks whole functions as unlikely or likely execution paths |
| `MICROFMT_CONSUMABLE(...)` and typestate macros | Describes monotonic object states to Clang's consumed analysis |

The macros expand to compiler attributes when supported and otherwise expand
to nothing. They must therefore improve diagnostics without changing program
semantics or becoming the only enforcement of a lifetime rule.

Use the stronger contract macros only when their requirements hold on every
path. `MICROFMT_ASSUME_ALIGNED` makes misaligned returns undefined behavior,
`MICROFMT_MUSTTAIL` requires ABI-compatible caller and callee signatures, and
the consumable-state macros suit one-way state transitions rather than
resettable or idempotent objects. `MICROFMT_MALLOC_PAIR` is for heap-like GCC
allocators; it does not apply to `scratch_allocator`, which returns borrowed
storage and has no deallocation operation.

### Consumed-state (`-Wconsumed`) annotations

`MICROFMT_CONSUMABLE`, `MICROFMT_CALLABLE_WHEN`, `MICROFMT_SET_TYPESTATE`, and
`MICROFMT_RETURN_TYPESTATE` describe monotonic "unconsumed → consumed" object
states to Clang's `-Wconsumed` analysis. They suit RAII writers with a
close/finalize operation after which further writes are invalid, such as
`json::object_writer`/`array_writer` and `cbor::map_writer`/`array_writer`:

```cpp
class MICROFMT_CONSUMABLE(unconsumed) object_writer {
public:
  explicit object_writer(sink out) noexcept MICROFMT_RETURN_TYPESTATE(unconsumed);

  object_writer &key(microfmt::string_view k) noexcept MICROFMT_CALLABLE_WHEN("unconsumed");

  void end() noexcept MICROFMT_CALLABLE_WHEN("unconsumed", "consumed")
      MICROFMT_SET_TYPESTATE(consumed);
};
```

Every constructor that yields a fresh, usable object — including move
constructors — needs an explicit `MICROFMT_RETURN_TYPESTATE(unconsumed)`;
without it, Clang treats the object as already consumed and warns on the
first legitimate call. Idempotent close/finalize methods should list every
state from which they may legally be called (for example
`MICROFMT_CALLABLE_WHEN("unconsumed", "consumed")`) so that a repeat call from
a destructor is not itself flagged. Unlike `MICROFMT_CONSUMABLE`,
`MICROFMT_SET_TYPESTATE`, and `MICROFMT_RETURN_TYPESTATE`, which take bare
state identifiers, `MICROFMT_CALLABLE_WHEN` requires its state names as
quoted string literals.

Strict Clang builds explicitly enable the supported `-Wdangling`,
`-Wdangling-gsl`, `-Wdangling-assignment-gsl`, `-Wdangling-field`, and
`-Wreturn-stack-address` diagnostics. CMake probes each flag before adding it,
so older Clang and AppleClang releases remain supported. These diagnostics are
treated as errors when `JPLCZ_MICROFMT_ENABLE_STRICT_WARNINGS` is enabled.

Clang's `-Wunsafe-buffer-usage` is a separate bounds-migration analysis. It is
not part of the default warning set because microfmt deliberately contains
audited low-level buffer primitives. Use the unsafe-buffer annotation and
pragma macros below when running that analysis separately.

Run the current Clang 24 migration check with:

```bash
./scripts/check-unsafe-buffer-usage.sh
```

The check covers the public headers in C++17, C++20, and C++23 modes. It
always prints the full diagnostic report for each standard, plus a per-file
warning-count summary, and fails if any standard produces more than
`MICROFMT_UNSAFE_BUFFER_MAX_WARNINGS` (default `0`) diagnostics — i.e. the
headers must be entirely clean of unsafe-buffer-usage warnings by default.
Raise `MICROFMT_UNSAFE_BUFFER_MAX_WARNINGS` temporarily while migrating a
batch of call sites.

Use `MICROFMT_LIFETIMEBOUND` on parameters or accessors whose result borrows
from an input or from `*this`. Mark owning containers with `MICROFMT_OWNER`
and non-owning views or reference wrappers with `MICROFMT_POINTER`. Add
`MICROFMT_LIFETIME_CAPTURE_BY_THIS` to constructors or member functions that
retain an input borrow in the object. Use `MICROFMT_LIFETIME_CAPTURE_BY(...)`
when another named parameter is the capturer instead. On constructors,
`MICROFMT_LIFETIMEBOUND` and capture-by-`this` have equivalent Clang lifetime
semantics; capture-by-`this` is additionally useful for void-returning setters:

```cpp
class MICROFMT_POINTER packet_view {
public:
  explicit packet_view(
      packet &source MICROFMT_LIFETIMEBOUND
          MICROFMT_LIFETIME_CAPTURE_BY_THIS) noexcept;

  void set_scratch(
      span<std::byte> scratch
          MICROFMT_LIFETIME_CAPTURE_BY_THIS) noexcept;

  const packet *
  get() const noexcept MICROFMT_LIFETIMEBOUND;
};
```

The public API applies these contracts to the main borrowing boundaries:

- Type-erased handles such as `sink`, `address_space_ref`,
  `symbol_resolver_ref`, and `frame_unwinder_ref` are pointer-like.
- Inspector views bind retained scratch spans and caller-owned contexts with
  `MICROFMT_LIFETIMEBOUND`.
- `scratch_allocator` allocations and remaining spans are tied to the
  allocator and its caller-provided backing storage.
- Sink adapters bind `as_sink()` to the adapter object, while buffer accessors
  such as `view()`, `as_span()`, and `c_str()` bind returned views or pointers
  to the sink object.

For example, keep both the adapter and its backing buffer alive while using a
type-erased sink:

```cpp
char storage[128]{};
microfmt::span_sink adapter(storage);
microfmt::sink out = adapter.as_sink();

microfmt::format_to(out, MICROFMT_STRING("{}"), 42);
auto text = adapter.view();
```

Likewise, an inspector view that accepts scratch storage or a resolution
context does not take ownership. Those arguments must outlive the view and any
derived pointer, span, or string view.

Prefer deleted rvalue overloads, reference-qualified accessors, and constrained
constructors for portable enforcement. An annotation alone cannot prevent a
dangling reference on compilers that ignore it.

## Unsafe buffer boundaries

`MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE` and
`MICROFMT_END_UNSAFE_BUFFER_USAGE` delimit implementation regions that
intentionally perform raw buffer operations under Clang's safe-buffer
analysis. Keep these regions narrow and place checked public APIs around them.

The implementations of `span`, `array`, and `scratch_allocator` form the first
such audited boundaries. Their public operations perform bounds and capacity
checks, while their implementations necessarily use built-in arrays and
pointer arithmetic to provide C++17-compatible storage primitives.

`microfmt::unsafe::ptr_cast` and `microfmt::unsafe::unchecked_address` make
explicit the points where code leaves ordinary type and lifetime guarantees.
Use them only after validating alignment, bounds, mutability, and owner
lifetime.
