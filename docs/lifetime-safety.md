<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Lifetime safety, `value_ref`, and `value_ptr`

microfmt combines type-level borrowing rules with optional compiler
annotations. The type system provides portable enforcement, while annotations
give supporting compilers additional information for diagnostics and static
analysis.

`microfmt::value_ref<T>`, `microfmt::value_ptr<T>`, `microfmt::checked_value<T>`,
and the `RELOCO_*` annotation macros (`RELOCO_LIFETIMEBOUND`, `RELOCO_OWNER`,
`RELOCO_POINTER`, `RELOCO_UNSAFE_BUFFER_USAGE`, the consumed-state family, and
the rest) are provided by
[`jplcz_reloco`](https://github.com/jplcz/reloco), a dependency of microfmt,
and re-exported under `microfmt::`; use the `RELOCO_*` macros directly rather
than a microfmt-specific alias. See reloco's
[Lifetime safety](https://github.com/jplcz/reloco/blob/master/docs/lifetime-safety.md)
guide — including the `checked_value<T>` typestate diagrams — for the full
API reference, the annotation-macro table, and a step-by-step guide to adding
consumed-state tracking to your own types.

## Where microfmt uses these types

`value_ref` is part of the main library interface rather than the inspector
subdirectory. The inspector's `concrete_metadata_map` uses `value_ref` to
store heterogeneous borrowed properties (see
[Inspector: metadata maps](inspector/metadata.md)). Read-only adapters such as
`hash_view`, `variant_view`, and the `{fmt}` compatibility output iterator
store `value_ref<const T>` explicitly.

The library uses `value_ptr` for retained type-erased inspector contexts,
symbol-resolution contexts, callback and container targets, metadata value
pointers, and PMR resources. Owning allocation links, static virtual tables,
and public C-style callback ABI fields remain raw pointers.

## Consumed-state annotations in microfmt's own writers

microfmt's `json::object_writer`/`array_writer` and
`cbor::map_writer`/`array_writer` use the `RELOCO_CONSUMABLE`/
`RELOCO_CALLABLE_WHEN`/`RELOCO_SET_TYPESTATE`/`RELOCO_RETURN_TYPESTATE`
family (see reloco's guide for the mechanics) to flag writes after `end()`
under Clang's `-Wconsumed`. Because Clang's consumed analysis cannot see
through a reference or pointer parameter, an object arriving as
`object_writer &` (for example the writer a `json_obj`/`cbor_map` callback
receives) starts in the `unknown` state rather than `unconsumed`. These
writer types expose an `as_known()` helper for exactly this boundary — it is
callable from `unconsumed` or `unknown`, asserts `!closed_` at runtime, and
returns to `unconsumed` so the rest of the chain is tracked normally:

```cpp
microfmt::json::json_obj([](microfmt::json::object_writer &event) {
  event.as_known().kv("kind", "boot").kv("sequence", 7);
});
```

Reach for `as_known()` only at boundaries where the parameter or member truly
is unconsumed by construction (as callback parameters freshly constructed by
the caller are); it is a targeted escape hatch for Clang's analysis limits,
not a way to silence a genuine reuse-after-`end()` warning.

### `RELOCO_UNSAFE_BUFFER_USAGE` covers the whole library, not just these types

The explicitly-unsafe tier is not unique to `value_ptr`, `value_ref`, and
`checked_value`. Every pre-existing `unsafe_*` accessor in the hardened
containers — `array::unsafe_at`/`unsafe_front`/`unsafe_back`,
`span::unsafe_subspan`/`unsafe_data`/`unsafe_at`/`unsafe_front`/
`unsafe_back`/`unsafe_first`/`unsafe_last`, and
`string_view::unsafe_front`/`unsafe_back`/`unsafe_substr`/`unsafe_data`/
`unsafe_remove_prefix`/`unsafe_remove_suffix` — is also marked
`RELOCO_UNSAFE_BUFFER_USAGE`, including the internal uses inside the core
`vformat_to` formatting loop itself. This makes the whole library's
explicitly-unsafe tier uniformly enforced: an unwrapped call to *any*
`unsafe_*` method, anywhere, is a Clang `-Wunsafe-buffer-usage` diagnostic,
not just a documented convention. `scripts/check-unsafe-buffer-usage.sh` is
the acceptance gate that keeps this at zero diagnostics.

### When not to use it

- The object can be freely reset or reused (state is not monotonic).
- The terminal transition depends on data the compiler cannot see (for
  example, a network response deciding whether the object is still usable) —
  model that with a runtime check and a normal return value instead.
- The type is only ever accessed through type-erased function pointers or
  virtual dispatch that Clang's local, syntactic analysis cannot follow (see
  `gdb_packet_writer`'s callback-based sink for an example already in this
  codebase); keep the runtime guard as the sole enforcement there.

Strict Clang builds explicitly enable the supported `-Wdangling`,
`-Wdangling-gsl`, `-Wdangling-assignment-gsl`, `-Wdangling-field`,
`-Wreturn-stack-address`, and `-Wconsumed` diagnostics. CMake probes each flag
before adding it, so older Clang and AppleClang releases remain supported.
These diagnostics are treated as errors when
`JPLCZ_MICROFMT_ENABLE_STRICT_WARNINGS` is enabled.

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

Use `RELOCO_LIFETIMEBOUND` on parameters or accessors whose result borrows
from an input or from `*this`. Mark owning containers with `RELOCO_OWNER`
and non-owning views or reference wrappers with `RELOCO_POINTER`. Add
`RELOCO_LIFETIME_CAPTURE_BY_THIS` to constructors or member functions that
retain an input borrow in the object. Use `RELOCO_LIFETIME_CAPTURE_BY(...)`
when another named parameter is the capturer instead. On constructors,
`RELOCO_LIFETIMEBOUND` and capture-by-`this` have equivalent Clang lifetime
semantics; capture-by-`this` is additionally useful for void-returning setters:

```cpp
class RELOCO_POINTER packet_view {
public:
  explicit packet_view(
      packet &source RELOCO_LIFETIMEBOUND
          RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept;

  void set_scratch(
      span<std::byte> scratch
          RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept;

  const packet *
  get() const noexcept RELOCO_LIFETIMEBOUND;
};
```

The public API applies these contracts to the main borrowing boundaries:

- Type-erased handles such as `sink`, `address_space_ref`,
  `symbol_resolver_ref`, and `frame_unwinder_ref` are pointer-like.
- Inspector views bind retained scratch spans and caller-owned contexts with
  `RELOCO_LIFETIMEBOUND`.
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

`RELOCO_BEGIN_UNSAFE_BUFFER_USAGE` and
`RELOCO_END_UNSAFE_BUFFER_USAGE` delimit implementation regions that
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
