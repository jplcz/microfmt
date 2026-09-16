<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Inspector: metadata maps

The metadata map types attach heterogeneous, formatted properties to
diagnostic objects without allocation or virtual dispatch.

## Choose the appropriate type

`metadata_map` is a type-erased, non-owning generator view. It stores a
`void *` context and a `metadata_next_fn_t` callback. Each `get_next` call asks
the callback to populate one `property_entry`; returning `false` ends
iteration.

`concrete_metadata_map` manages a fixed-capacity table of `property_entry`
objects supplied by the caller. It does not allocate the table or copy the
property values. Use it when metadata is assembled locally and then exposed
as a `metadata_map`.

## Building a concrete map

```cpp
#include <microfmt/inspector/concrete_metadata_map.hpp>

microfmt::property_entry entries[4];
microfmt::concrete_metadata_map metadata(entries);

int priority = 5;
bool supervised = true;

metadata.set("priority", microfmt::value_ref(priority));
metadata.set("supervised", microfmt::value_ref(supervised));

microfmt::concrete_metadata_map::span_iteration_state state;
auto view = metadata.make_view(state);
microfmt::format_to(output, MICROFMT_STRING("{}"), view);
// {"priority": 5, "supervised": true}
```

`set` returns `false` when a new key would exceed the supplied capacity.
Setting an existing key updates its borrowed value and formatter without
increasing `size()`. Values can have different formattable types.

The keys, entry storage, values, concrete map, and iteration state are all
caller-owned. They must remain alive until the view has finished formatting.
Both keys and values are borrowed; `value_ref` prevents temporary values but
does not extend lvalue lifetimes.

## Iteration state

Formatting consumes the current generator state. Call `make_view(state)` again
to reset the state before another traversal:

```cpp
auto first = metadata.make_view(state);
microfmt::format_to(output, MICROFMT_STRING("{}"), first);

priority = 7;
auto second = metadata.make_view(state);
microfmt::format_to(output, MICROFMT_STRING("{}"), second);
```

Do not create a view from a temporary `concrete_metadata_map`; the rvalue
overload is deleted. A `metadata_map` constructed directly from a callback
similarly requires its callback context to outlive all calls to `get_next`.

## Direct generator views

For metadata produced incrementally, implement a `noexcept` callback with this
signature:

```cpp
bool next(void *ctx, microfmt::property_entry &out) noexcept;
```

Populate `out.key`, `out.val_ptr`, and `out.print_fn`, then advance the context.
The callback controls ordering and termination. A null context or callback
makes `get_next` return `false`. If either the value pointer or print function
in an entry is null, that entry formats as `null`.

The formatter emits JSON-like diagnostic text. Keys are surrounded with
quotes but are not escaped, so use trusted, prevalidated diagnostic keys.

## Markdown tables

`microfmt::md::write_metadata_table` consumes a `metadata_map` and writes each
property as a row in a two-column Markdown table. Supply a character scratch
buffer large enough for the longest formatted value:

```cpp
microfmt::concrete_metadata_map::span_iteration_state state;
char value_scratch[64];

microfmt::md::writer document(output);
microfmt::md::write_metadata_table(
    document, metadata.make_view(state), value_scratch, "Runtime Metadata");
```

See `examples/metadata_table_demo.cpp` for a complete application that streams
the generated document to standard output.
