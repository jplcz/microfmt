<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: MIT
-->

# Inspector: remote containers

Remote container views traverse target data one element at a time. They do not
materialize a host-side container, which keeps allocation and stack use
bounded.

## The common container view

`remote_container_view` holds a target container address, an
`address_space_ref`, caller-owned byte scratch, a context, and
`container_options`. The context is created with `make_container_context` and
contains traversal state plus a callback that writes entries to a sink.

`container_options` controls delimiters, key/value rendering, the entry
separator, and `max_print`. Set `max_print` for crash-time or untrusted
structures so a corrupt cycle or implausibly large size cannot produce
unbounded output.

The context must outlive its `remote_container_view`. It is usually a local
object in the code that immediately calls `format_to`, or a member of a
long-lived inspection session.

## Standard layout helpers

Use the concrete helpers when the target has a conventional layout:

| Header | Helper | Target layout |
|---|---|---|
| `remote_vector.hpp` | `remote_vector_traits::vector_layout` | data pointer, size, optional capacity |
| `remote_vector.hpp` | `remote_vector_traits::carray_layout` | fixed contiguous array |
| `remote_forward_list.hpp` | `remote_forward_list_traits::forward_list_layout` | head pointer and node next/data offsets |
| `remote_hash_table.hpp` | `remote_hash_table_traits::chaining_layout` | bucket array with singly linked node chains |
| `remote_binary_tree.hpp` | `remote_binary_tree_traits::bst_layout` | root pointer and left/right/key/value node offsets |

Each helper accepts target pointer and size types as template parameters where
they can differ from the host. The result is a callable: invoke it with the
remote container address to create the context, then construct a
`remote_container_view`.

```cpp
auto make_context = microfmt::remote_vector_traits::vector_layout<uint32_t>(
    data_offset, size_offset, capacity_offset);
auto context = make_context(remote_vector_address);

microfmt::remote_container_view view{
    remote_vector_address, space, scratch, &context};
microfmt::format_to(output, MICROFMT_STRING("values={}"), view);
```

## Custom container implementations

For a proprietary container, provide a type-erased vtable and use the matching
`make_remote_*_context` function. Vector vtables resolve size and element
addresses; list, tree, and hash-table vtables resolve traversal links and
format individual nodes or entries.

Callbacks receive the address space, scratch span, and output sink. Read only
the current node or element into scratch, format it, and then continue. Do not
copy the full container into a temporary array.

## Faults and traversal bounds

The views render `<fault>` when a required node read, link read, or element
format fails. Empty or missing roots and heads render an empty container.
Tree traversal uses a scratch-backed bounded node stack, so provide enough
scratch for both that stack and an element read. Deep or malformed trees are
bounded rather than recursively walked.

Hash and list layouts cannot infer cycles from raw pointers. Always choose a
practical `max_print` value when reading arbitrary target memory. Treat
truncated output or a fault marker as diagnostic information, not proof that
the target structure is valid.
