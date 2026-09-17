<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Inspector: memory and remote objects

This subsystem turns target addresses into bounded formatter views. It is the
foundation for every other inspector component.

## Address spaces

`address_space_ref` is a small type-erased handle over byte reads, optional
byte writes, and string reads. Specialize `address_space_traits<Tag>` for a
transport tag, then construct an `address_space_ref` from the tag and, for a
stateful transport, its context.

```cpp
struct dump_reader_tag {};
struct dump_reader_context {
  // Owns the dump mapping and range-validation state.
};

template <> struct microfmt::address_space_traits<dump_reader_tag> {
  using context_type = dump_reader_context;

  static bool read_bytes(const void *context, uintptr_t address,
                         void *destination, size_t size) noexcept;
  static bool write_bytes(const void *context, uintptr_t address,
                          const void *source, size_t size) noexcept;
  static bool read_string(const void *context, uintptr_t address,
                          char *destination, size_t capacity, size_t &size,
                          bool &terminated) noexcept;
};

dump_reader_context reader;
auto space = microfmt::address_space_ref{dump_reader_tag{}, reader};
```

The transport owns address validation. It must return `false` rather than read
or write an invalid range. It should handle target byte order, access policy,
and address translation before copying. `write_bytes` is optional; omit it for
a read-only transport. `address_space_ref::can_write()` reports whether the
bound backend supplies the operation. The handle does not take ownership of
its context.

The trait callbacks retain their minimal `bool` ABI, while the public
`address_space_ref` operations return `microfmt::expected`:

```cpp
auto result = space.read<uint32_t>(target_address);
if (!result) {
  switch (result.error()) {
  case microfmt::address_space_error::invalid_handle:
  case microfmt::address_space_error::invalid_address:
  case microfmt::address_space_error::invalid_buffer:
  case microfmt::address_space_error::empty_buffer:
  case microfmt::address_space_error::read_failed:
    // Apply transport-specific fault policy.
    break;
  }
} else {
  uint32_t value = *result;
}
```

`read_bytes` and the output-parameter `read(address, object)` overload return
`expected<void, address_space_error>`. The value-returning
`read<T>(address)` returns `expected<T, address_space_error>` and requires a
trivially copyable, nothrow default-constructible, nothrow move-constructible
`T`. Backend callback failure is reported as `read_failed`; the wrapper
distinguishes invalid handles, zero addresses, and invalid buffers before
dispatch.

`write_bytes` and `write(address, object)` use the same validation model.
Writes distinguish `write_unsupported` from `write_failed`, allowing callers
to tell a read-only transport from a rejected target write:

```cpp
uint32_t replacement = 0x1234;
auto result = space.write(target_address, replacement);
if (!result) {
  switch (result.error()) {
  case microfmt::address_space_error::write_unsupported:
    // The transport is read-only.
    break;
  case microfmt::address_space_error::write_failed:
    // The backend rejected or could not complete the write.
    break;
  default:
    // Invalid handle, address, or input buffer.
    break;
  }
}
```

`local_space_tag` is the built-in writable transport for local addresses. Use
it in tests or self-inspection only; it does not make arbitrary addresses safe
to read or write.

## Translate virtual addresses

`address_translator_ref` type-erases a platform virtual-to-physical
translation backend. Specialize `address_translator_traits<Tag>` with a
`context_type` and `translate` operation:

```cpp
struct page_table_tag {};
struct page_table_context {
  // Page-table root and target-specific translation state.
};

template <> struct microfmt::address_translator_traits<page_table_tag> {
  using context_type = page_table_context;

  static bool
  translate(const void *context, uintptr_t virtual_address,
            microfmt::translation_attributes &attributes) noexcept;
};

page_table_context page_tables;
auto translator =
    microfmt::address_translator_ref::make<page_table_tag>(page_tables);
```

On success, `translation_attributes` reports the physical address, target
space ID, security state, and read/write/execute/user permissions.
`translation_attributes::is_valid()` is true when at least one access
permission is present.

Both stateless (`context_type = void`) and stateful translators are supported.
The context is borrowed and must outlive the handle. A failed translation
should return `false` without publishing a partially initialized result.

See `examples/address_translator_demo.cpp` for a fixed mapping-table backend.

## Walk remote page tables

`remote_page_table_walker` performs an architecture-neutral virtual-to-physical
walk using an `address_space_ref` that reads **physical** memory. Architecture
code supplies callbacks that describe each level and decode raw entries:

```cpp
microfmt::remote_page_table_callbacks callbacks{
    level_count,
    describe_level,
    decode_entry,
};
microfmt::remote_page_table_layout_ref layout(layout_state, callbacks);
microfmt::remote_page_table_walker walker(physical_space, layout);

microfmt::remote_page_table_walk_step step_storage[4];
microfmt::remote_page_table_walk_trace trace(step_storage);
auto result = walker.walk(root_table_physical_address,
                          target_virtual_address, trace);
```

`describe_level` supplies the virtual-address index shift and width plus the
entry size. Entries are decoded as little-endian values; sizes of 1, 2, 4,
and 8 bytes are supported.
`decode_entry` classifies the normalized raw value as invalid, a pointer to the
next physical table, or a leaf mapping. A leaf supplies its physical page
base, page size, space ID, security state, and access permissions.

The walker validates index and address arithmetic, reads each entry through
the physical address space, and rejects malformed leaves or next-table entries
at the final level. It returns `expected<remote_page_table_walk_result,
remote_page_table_walk_error>` with the final physical address and
`translation_attributes`.

The trace uses caller-owned bounded storage and records table addresses, entry
addresses, raw values, and decoded entries. Walking continues if that storage
fills; `trace.truncated()` reports that diagnostic steps were omitted. On a
failed walk, the trace still contains every recorded step before the failure.

The generic walker deliberately does not embed an ARM, x86, or RISC-V entry
format. Platform traits can implement stage, granule, huge-page, security, and
permission rules without changing traversal or physical-memory access.

## Classify memory regions

`memory_classifier_ref` maps a virtual address to a
`memory_region_info`. Region descriptions contain half-open
`[start_address, end_address)` bounds, an address-space ID, protection flags,
and a `memory_region_type` such as:

* kernel or user code and data;
* kernel or process stacks;
* direct-map and page-descriptor regions;
* kernel heap memory;
* device MMIO and guard pages; or
* secure/non-secure mappings.

`memory_region_info::contains(address)` performs the half-open bounds check.
Implement `memory_classifier_traits<Tag>::classify_address` using the same
stateless or borrowed-context pattern as other inspector references:

```cpp
template <> struct microfmt::memory_classifier_traits<layout_tag> {
  using context_type = layout_context;

  static bool classify_address(
      const void *context, uintptr_t virtual_address,
      microfmt::memory_region_info &region) noexcept;
};
```

Return `false` for unknown or unmapped addresses. The classifier describes
policy and layout; it does not itself read target memory.

See `examples/memory_classifier_demo.cpp` for a region-table implementation.

## Scan likely memory addresses

`memory_scanner` combines an `address_space_ref`,
`memory_classifier_ref`, and one of two address sources:

* a `register_context_ref` plus an explicit `span<const uintptr_t>`; or
* an `address_source_ref` callback that yields addresses incrementally.

When a register context is supplied, the scanner visits
`AbiTraits::register_traits::address_registers()` in probability order. This
uses likely pointer-bearing GPRs while excluding SP, LR/RA, PC, fixed-zero
registers, and zero-valued candidates. Explicit addresses are scanned after
the register candidates.

```cpp
static microfmt::array<char, 128> symbol_scratch;
static microfmt::memory_scanner_context scanner_context;
scanner_context.options.symbol_resolver = resolver;
scanner_context.symbol_scratch =
    {symbol_scratch.data(), symbol_scratch.size()};

microfmt::memory_scanner::scan_and_dump<microfmt::aarch64_abi_traits>(
    target_space, classifier, register_context, explicit_addresses,
    scanner_context, output);
```

Only the register-based overload needs `AbiTraits`, because it uses the
architecture's register width and ordered pointer-candidate list. The
`address_source_ref` overload is ABI-independent and is called without a
template argument.

Addresses classified as kernel/user code or data are resolved with the
optional `symbol_resolver_ref` and printed before any potential dump.
`symbol_scratch` is borrowed caller-owned storage passed directly to the
resolver. Keep it in static, arena, or heap storage when scanner stack usage
must remain minimal; the scanner does not allocate an internal symbol buffer.
`memory_scanner_context` similarly owns reusable region metadata, raw and
derived symbol results, the hex-dump descriptor and line buffer, and the
address-space handle used by the checked reader. The caller controls its
placement and must not share one context between concurrent scans.

For readable data regions, the scanner uses `hexdump_checked` to render at most
80 bytes, 16 bytes per line, with a hexadecimal and printable-ASCII pane.
Reads are bounded by the classified region's exclusive end address, so a dump
does not cross into the next known mapping. The implementation streams one
line at a time instead of storing the complete 80-byte dump.

The scanner does not dereference unknown, unreadable, executable, direct-map,
MMIO, guard-page, or non-secure regions. A safe `address_space_ref` remains
mandatory because classification data can itself be stale or incorrect.

`address_source_ref` borrows its source context and calls
`bool next(const void *, uintptr_t &) noexcept` until it returns `false`. This
is useful for scanning stack slots, allocator metadata, saved contexts, or
other incrementally produced address sets without allocation.

See `examples/memory_scanner_demo.cpp` for combined register and explicit
address scanning.

## Reuse remote layout accessors

`remote_layout_query<Value, Source>` provides a uniform operation for reading
one property from a remote object. It owns a
`remote_layout_accessor<Value, Source>`, which can use either a callback or a
fixed offset with an explicit target-side storage type.

Use `make_remote_offset_query<Value, Stored>()` when a field is available at a
known offset:

```cpp
auto size_query =
    microfmt::make_remote_offset_query<size_t, uint32_t>(
        offsetof(target_vector, size));

size_t size = 0;
if (!size_query(target_space, target_vector_address, size)) {
  // The field could not be read or represented as size_t.
}
```

`Stored` describes the target representation while `Value` is the local value
returned to the inspector. Unsigned integer conversions are checked, so a
64-bit target value that does not fit a 32-bit local result fails instead of
being truncated. Positive and negative offsets are supported.

Use `make_remote_layout_query<Value>()` for layouts that require decoding,
tag inspection, pointer authentication, or another non-trivial operation:

```cpp
struct data_reader {
  target_string_layout layout;

  bool operator()(microfmt::address_space_ref space,
                  uintptr_t object_address, uintptr_t &data_address,
                  size_t decoded_size) const noexcept {
    return layout.resolve_data(
        space, object_address, decoded_size, data_address);
  }
};

auto data_query =
    microfmt::make_remote_layout_query<uintptr_t>(data_reader{layout});
```

Callback objects and their state are retained by value. They must be
nothrow-copyable, and their call operator must be `noexcept`, return `bool`,
and accept the address space, object address, output reference, then any
query-specific inputs. This allows one decoded property to feed a later query,
such as passing a string length to an inline-versus-allocated data resolver.

## Strings and pointer-width compatibility

`remote_string_view` and `foreign_string_view` read a remote NUL-terminated
string in chunks through a supplied `span<char>`. The view contains the
address, transport, scratch span, and maximum render length; it does not copy
the complete string into a formatter-local buffer.

`remote_basic_string_view<Layout>` inspects a remote C++ string object whose
character count is stored separately from its data. Unlike
`remote_string_view`, it reads exactly the decoded length and therefore
preserves embedded NUL characters. Create layouts with
`remote_basic_string_traits`:

```cpp
struct target_string {
  uint32_t data;
  uint32_t size;
};

char scratch[32];
auto layout =
    microfmt::remote_basic_string_traits::
        pointer_size_layout<uint32_t, uint32_t>(
            offsetof(target_string, data), offsetof(target_string, size));
auto string = microfmt::make_remote_basic_string_view(
    target_string_address, target_space, scratch, layout, 256);

microfmt::format_to(output, MICROFMT_STRING("{}"), string);
```

`pointer_size_layout` supports representations whose data field always points
at the active character storage, including implementations that redirect that
pointer to an inline buffer for short strings. `size_selected_layout` supports
representations that use inline storage when `size <= inline_capacity` and a
remote pointer otherwise. The pointer and size representation types must
match the target ABI; use `uint32_t` for a 32-bit target inspected by a 64-bit
host.

Every layout performs two distinct operations: reading the string length and
resolving the character-data address. These operations use
`remote_layout_query`, which owns a templated `remote_layout_accessor`.
An accessor can retain either a callback or a typed fixed-offset description:

```cpp
auto size_query =
    microfmt::make_remote_offset_query<size_t, uint32_t>(size_offset);
auto custom_query = microfmt::make_remote_layout_query<uintptr_t>(
    [state](microfmt::address_space_ref space, uintptr_t object_address,
            uintptr_t &result, size_t decoded_size) noexcept {
      return state.resolve_data(space, object_address, decoded_size, result);
    });
```

The fixed-offset query reads the specified target-side storage type and safely
converts it to the query's public value type. Callback objects and captured
state are retained by value.

For standard-library or application-specific string encodings, combine two
callback readers:

```cpp
auto layout = microfmt::remote_basic_string_traits::callback_layout(
    read_target_string_size,
    read_target_string_data_address);
```

The data reader receives the output address reference followed by the already
decoded size, allowing it to select inline or allocated storage without
rereading the length. Use `pointer_size_layout` or `size_selected_layout` when
offsets completely describe the representation instead. The address space and
scratch storage are borrowed and must outlive the view. Formatting is bounded
by `max_limit` and appends `...` only when the decoded string is longer than
that limit.

Direct callers can use `address_space_ref::read_string_chunk`, which returns
`expected<string_chunk, address_space_error>`. `string_chunk::length` reports
the bytes copied and `string_chunk::null_terminated` reports whether that
chunk encountered the terminator. An empty scratch span is reported as
`address_space_error::empty_buffer`.

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

For direct access, `remote_ref<T>::load()` returns
`expected<T *, remote_load_error>` and `remote_object_view::load_raw()` returns
`expected<const void *, remote_load_error>`. The error distinguishes a null
target address, undersized or misaligned scratch storage, an invalid address
space, and a backend read failure:

```cpp
auto loaded = remote.load();
if (loaded) {
  const task_info &task = **loaded;
  // Use task only while the remote reference's scratch storage remains valid.
}
```

`remote_field_traits<T>` is the extension point for custom field renderers.
Specialize it for a wrapper type when reading a field needs target-aware logic.
The default trait formats the locally loaded field using `formatter<T>`.

## Smart-pointer fields

`remote_smart_ptr.hpp` provides views and layout wrappers for unique, shared,
and intrusive pointers. Their views read the pointer representation from the
target, detect nulls, then render the pointee through a registered
`remote_object_view` or a bounded local copy.

Pointer, control-block, strong-count, weak-count, and intrusive-count fields
are read through typed `remote_layout_query` instances. The view constructors
still accept offsets, while the query layer applies the target representation
type and checked pointer conversion consistently with remote containers and
strings.

Shared and intrusive pointer views can also render reference counts when their
target-layout offsets are known. These offsets are ABI and standard-library
implementation details; configure them per target rather than assuming host
offsets.

## Lifetime and failure behavior

The remote-object view, address-space context, descriptors, and scratch span
must all remain valid through formatting. If a required read fails, the
formatter emits a fault marker. Direct loading APIs return
`microfmt::expected`; inspect the error before retrying, switching transports,
or using an emergency scratch pool. Do not rely on partial output as a
successful remote-object read.
