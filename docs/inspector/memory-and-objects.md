<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: MIT
-->

# Inspector: memory and remote objects

This subsystem turns target addresses into bounded formatter views. It is the
foundation for every other inspector component.

## Address spaces

`address_space_ref` is a small type-erased handle over two operations:
`read_bytes` and `read_string`. Specialize `address_space_traits<Tag>` for a
transport tag, then construct an `address_space_ref` from the tag and, for a
stateful reader, its context.

```cpp
struct dump_reader_tag {};
struct dump_reader_context {
  // Owns the dump mapping and range-validation state.
};

template <> struct microfmt::address_space_traits<dump_reader_tag> {
  using context_type = dump_reader_context;

  static bool read_bytes(const void *context, uintptr_t address,
                         void *destination, size_t size) noexcept;
  static bool read_string(const void *context, uintptr_t address,
                          char *destination, size_t capacity, size_t &size,
                          bool &terminated) noexcept;
};

dump_reader_context reader;
auto space = microfmt::address_space_ref{dump_reader_tag{}, reader};
```

The transport owns address validation. It must return `false` rather than read
an invalid range. It should handle target byte order and address translation
before copying into the supplied buffer. `address_space_ref` does not take
ownership of its context.

`local_space_tag` is the built-in transport for local addresses. Use it in
tests or self-inspection only; it does not make arbitrary addresses safe to
read.

## Strings and pointer-width compatibility

`remote_string_view` and `foreign_string_view` read a remote NUL-terminated
string in chunks through a supplied `span<char>`. The view contains the
address, transport, scratch span, and maximum render length; it does not copy
the complete string into a formatter-local buffer.

Use `compat32.hpp` and the `string32_ptr`/pointer wrappers in
`remote_object.hpp` when a 64-bit inspection host reads a 32-bit target. Keep
target pointer and size types explicit in container-layout traits as well.
Never reinterpret a host pointer as a target pointer without considering the
target ABI.

## Describe a remote structure

`remote_object_view` reads a registered object into a byte scratch span, then
formats its field descriptors. Register ordinary layouts with:

```cpp
struct task_info {
  uint32_t id;
  uint8_t state;
};

MICROFMT_REMOTE_STRUCT_BEGIN(task_info)
  MICROFMT_REMOTE_FIELD(id, uint32_t)
  MICROFMT_REMOTE_FIELD(state, uint8_t)
MICROFMT_REMOTE_STRUCT_END()
```

Create a `remote_object_view` with the object address, an `address_space_ref`,
`type_tag<task_info>{}`, and scratch storage. The scratch span must be at least
the registered structure size and correctly aligned for the structure.
Allocate additional caller-owned bytes after the object region when field
formatters need working storage, such as for remote strings or nested objects.

`remote_field_traits<T>` is the extension point for custom field renderers.
Specialize it for a wrapper type when reading a field needs target-aware logic.
The default trait formats the locally loaded field using `formatter<T>`.

## Smart-pointer fields

`remote_smart_ptr.hpp` provides views and layout wrappers for unique, shared,
and intrusive pointers. Their views read the pointer representation from the
target, detect nulls, then render the pointee through a registered
`remote_object_view` or a bounded local copy.

Shared and intrusive pointer views can also render reference counts when their
target-layout offsets are known. These offsets are ABI and standard-library
implementation details; configure them per target rather than assuming host
offsets.

## Lifetime and failure behavior

The remote-object view, address-space context, descriptors, and scratch span
must all remain valid through formatting. If a required read fails, the
formatter emits a fault marker. Do not rely on partial output as a successful
remote-object read.
