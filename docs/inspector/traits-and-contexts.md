<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Inspector: traits, contexts, and type erasure

For a minimal copy-paste provider skeleton, see
[Extending microfmt](../extending.md#3-context_type-based-provider-template).
This page covers the pattern rationale, worked examples, and lifetime rules
in depth.

Inspector providers use tags and trait specializations to select behavior at
compile time while exposing small type-erased `*_ref` handles to consumers.
This pattern avoids virtual inheritance, allocation, and caller-supplied
runtime vtables. It also makes the concrete context type, its ownership, and
its constness explicit.

The pattern has four parts:

1. an empty tag type identifies an implementation;
2. `provider_traits<Tag>` declares `context_type` and static operations;
3. a concrete typed wrapper or caller-owned context retains state; and
4. a `provider_ref` borrows that state and erases its concrete type.

Examples include `address_space_traits<Tag>` with `address_space<Tag>` and
`address_space_ref`; `address_translator<Tag>`, `symbol_resolver<Tag>`,
`memory_classifier<Tag>`, `frame_unwinder<Tag>`, and `exception_frame<Tag>`
with their corresponding `*_ref` types;
`register_context_traits<Tag>` with `register_context<Tag>` and
`register_context_ref`, and the remote container, ELF enumerator, exception
matcher, and unwind-hint registry APIs.

## Declaring a trait

Define an otherwise empty tag and specialize the relevant traits template in
namespace `microfmt`:

```cpp
struct process_memory_tag {};

struct process_memory_context {
  process_handle handle;
  uintptr_t lowest_address;
  uintptr_t highest_address;
};

template <>
struct microfmt::address_space_traits<process_memory_tag> {
  using context_type = process_memory_context;

  static bool read_bytes(
      microfmt::value_ref<const context_type> context, uintptr_t address,
                         void *destination, size_t size) noexcept {
    return read_process_memory(context->handle, address, destination, size);
  }

  static bool read_string(
      microfmt::value_ref<const context_type> context, uintptr_t address,
                          char *destination, size_t capacity,
                          size_t &length, bool &terminated) noexcept {
    return read_process_string(context->handle, address, destination, capacity,
                               length, terminated);
  }
};
```

`context_type` is part of the provider contract. A type-erased reference
accepts only contexts compatible with that declared type. It does not infer
behavior from arbitrary member functions and does not accept a runtime table
of function pointers.

Provider traits use `value_ref<const context_type>` for required read-only state and
`value_ref<context_type>` for operations that may mutate state.

For a provider with no state, declare `using context_type = void` and omit the
context parameter:

```cpp
struct identity_translator_tag {};

template <>
struct microfmt::address_translator_traits<identity_translator_tag> {
  using context_type = void;

  static bool translate(
      uintptr_t address,
      microfmt::translation_attributes &attributes) noexcept;
};

auto translator =
    microfmt::address_translator<identity_translator_tag>::ref();
```

## Context ownership and borrowing

Traits do not own state. Ownership belongs to either:

* a caller-owned context object borrowed directly by a `*_ref`; or
* a typed wrapper such as `address_space<Tag>`,
  `remote_forward_list<Tag>`,
  `unwind_hint_registry<Tag>`, `elf_image_enumerator<Tag>`,
  `exception_matcher<Tag>`, `address_source<Tag>`, or
  `register_context<Tag>`, which stores `traits::context_type` by value.

Direct borrowing is useful when one context is shared:

```cpp
microfmt::address_space<process_memory_tag> process{
    process_memory_context{handle, low, high}};
microfmt::address_space_ref space = process.ref();
```

An owning typed wrapper is useful when provider state naturally belongs to the
adapter:

```cpp
microfmt::unwind_hint_registry<platform_hint_tag> registry{
    platform_hint_context{table, table_size}};
microfmt::unwind_hint_registry_ref hints = registry.ref();
```

The typed wrapper exposes `context()` when callers need to inspect retained
state, update configuration before use, or verify counters in tests.

## Typed wrappers and erased references

A typed wrapper knows its `Tag`, `traits_type`, and `context_type`. Calls made
through it can be checked entirely at compile time. The wrapper normally owns
the context and returns a short-lived erased reference through `ref()` or, for
containers, a `remote_container_view` through `view(...)`.

The erased reference is the interoperability boundary. Higher-level
algorithms can accept one stable type:

```cpp
void walk_stack(microfmt::frame_unwinder_ref unwinder);
void scan(microfmt::address_source_ref source);
void find_hint(microfmt::unwind_hint_registry_ref registry);
```

Internally, an erased reference stores only a context pointer and pointers to
static thunks selected from the tag. The operation table is an implementation
detail of the erased boundary. It is not copied into each concrete provider
and is not supplied by callers at runtime.

## Const and mutable contexts

Use the borrow wrappers according to whether absence and mutation are valid:

* `value_ref<const T>`: required, non-null, read-only provider context;
* `value_ref<T>`: required, non-null context for a mutating operation;
* `value_ptr<const T>`: optional read-only state in an erased handle;
* `value_ptr<T>`: optional mutable state, for example when writes are disabled
  by binding a const context.

Most lookup and traversal traits receive `value_ref<const context_type>`.
Counters and caches that do not change the logical provider value may be
declared `mutable`:

```cpp
struct hint_context {
  const microfmt::unwind_hint *hints;
  size_t count;
  mutable size_t lookups;
};
```

Register providers separate read and write access. A read operation receives
`value_ref<const context_type>`, while a write operation receives
`value_ref<context_type>`. Constructing a `register_context_ref` from a const
context therefore preserves read access but disables writes without a
`const_cast`.

## How static dispatch reaches a `*_ref`

For a stateful provider, construction selects one static thunk table for its
tag:

```text
typed context
    -> provider_ref(Tag{}, context)
    -> static thunk for Tag
    -> provider_traits<Tag>::operation(value_ref<context_type>, ...)
```

The thunk casts the erased context pointer to
`traits::context_type const *`, creates the required `value_ref`, and calls the
trait operation. Where a provider supports optional operations, their presence
is detected at compile time and unavailable operations become null entries in
the erased table. No per-instance function table is stored in concrete
provider state.

The `*_ref` remains useful because algorithms and views do not need to become
templates over every transport, resolver, unwinder, or container layout.

## Remote forward-list example

A custom forward list supplies traversal and formatting operations:

```cpp
struct task_list_tag {};

struct task_list_context {
  uintptr_t head;
  size_t next_offset;
  size_t value_offset;
  mutable size_t visited;
};

template <>
struct microfmt::remote_forward_list_traits<task_list_tag> {
  using context_type = task_list_context;

  static bool get_head_node(
      microfmt::value_ref<const context_type> context, uintptr_t,
      microfmt::address_space_ref, microfmt::span<std::byte>,
      uintptr_t &node) noexcept {
    node = context->head;
    return true;
  }

  static bool get_next_node(
      microfmt::value_ref<const context_type> context,
      microfmt::address_space_ref space, microfmt::span<std::byte>,
      uintptr_t node, uintptr_t &next) noexcept {
    ++context->visited;
    return static_cast<bool>(
        space.read(node + context->next_offset, next));
  }

  static bool format_node_element(
      microfmt::value_ref<const context_type> context,
      microfmt::address_space_ref space, microfmt::span<std::byte>,
      uintptr_t node, const microfmt::sink &out) noexcept {
    uint32_t value = 0;
    if (!space.read(node + context->value_offset, value))
      return false;
    microfmt::format_to(out, "{}", value);
    return true;
  }
};

microfmt::remote_forward_list<task_list_tag> tasks{
    list_address, task_list_context{head, next_offset, value_offset, 0}};
auto view = tasks.view(space, scratch);
microfmt::format_to(output, "{}", view);
```

The wrapper retains the context by value. `remote_container_view` borrows the
wrapper and performs the final type erasure. The wrapper and scratch storage
must remain alive until formatting finishes.

Conventional layouts do not require a user specialization:

```cpp
auto tasks = microfmt::make_remote_forward_list<task_record>(
    list_address, head_offset, next_offset, value_offset);
auto view = tasks.view(space, scratch, options);
```

## Unwind-hint registry example

An unwind-hint provider follows the same pattern:

```cpp
struct platform_hints_tag {};

struct platform_hints_context {
  const microfmt::unwind_hint *entries;
  size_t count;
};

template <>
struct microfmt::unwind_hint_registry_traits<platform_hints_tag> {
  using context_type = platform_hints_context;

  static bool find_hint(
      microfmt::value_ref<const context_type> context, uintptr_t pc,
      microfmt::unwind_hint &result) noexcept {
    for (size_t index = 0; index < context->count; ++index) {
      if (context->entries[index].contains(pc)) {
        result = context->entries[index];
        return true;
      }
    }
    return false;
  }
};

microfmt::unwind_hint_registry<platform_hints_tag> registry{
    platform_hints_context{entries, entry_count}};
microfmt::unwind_hint_registry_ref hints = registry.ref();
```

For a bounded in-memory registry, use
`unwind_hint_registry_context<N>` with
`fixed_unwind_hint_registry_tag<N>`.

## Traits or callback objects?

Use traits when an object represents a reusable provider with a stable
operation set:

* address spaces, symbol resolvers, classifiers, and register contexts;
* ELF enumerators, unwinders, hint registries, and exception matchers;
* remote container traversal and element formatting.

Traits are especially appropriate when the provider is passed through a
type-erased `*_ref`, when several operations share one context, or when
operation availability should be checked at compile time.

Use a callback object when behavior is local to one algorithm invocation and
the callback itself is the policy rather than a reusable provider. Examples
include field accessors, visitor functions, and decode callbacks intentionally
bundled into an algorithm-specific descriptor. The remote page-table layout is
one such callback-based decoding policy: it describes a bounded walk rather
than a general inspector service.

Do not introduce a traits type solely to replace a short-lived lambda passed
directly to a templated algorithm. Conversely, do not expose runtime callback
tables from a reusable `*_ref` provider API.

## Lifetime requirements

Type erasure never transfers ownership:

* a directly borrowed context must outlive every derived `*_ref`;
* an owning typed wrapper must outlive every `ref()` or `view()` it creates;
* scratch spans and address-space contexts must outlive operations using them;
* do not construct a reference from a temporary context or wrapper;
* use `value_ref` for required borrows and `value_ptr` only where an empty or
  read-only/mutable optional state is meaningful.

Moving or destroying a typed owner invalidates references and views previously
created from it. Borrowing constructors and scratch parameters carry
`RELOCO_LIFETIMEBOUND` and `RELOCO_LIFETIME_CAPTURE_BY_THIS` where the
object retains them. Typed owners are marked `RELOCO_OWNER`; erased handles
and views are marked `RELOCO_POINTER`. Borrow-producing `context()`, `ref()`,
and `view()` operations are lvalue-qualified and their rvalue overloads are
deleted, preventing references from being obtained from temporary owners even
on compilers that ignore lifetime attributes.

## Testing conventions

Every provider traits implementation should have focused tests that:

1. bind a distinct tag and `context_type`;
2. call through the erased `*_ref` or `remote_container_view`, not only the
   trait directly;
3. verify arguments and results reach the correct static operation;
4. verify retained context state after one or more calls;
5. cover missing optional operations and const-context write rejection where
   applicable;
6. exercise failure propagation and bounded scratch behavior; and
7. compile through the C++17 public-header target.

Compile the tests with the repository's strict Clang configuration so
`-Wdangling`, `-Wdangling-field`, `-Wdangling-gsl`, and
`-Wreturn-stack-address` remain errors. Add negative `std::is_constructible`
checks when a borrowing constructor must reject temporary contexts.

Prefer counters, last-argument fields, and small fixed arrays in test contexts.
They prove dispatch and state retention without allocation or global mutable
state.
