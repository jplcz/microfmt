<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Inspector: remote containers

Remote container views traverse target data one element at a time. They do not
materialize a host-side container, which keeps allocation and stack use
bounded.

## The common container view

`remote_container_view` holds a target container address, an
`address_space_ref`, caller-owned byte scratch, a context, and
`container_options`. Typed containers own their traits context by value and
produce this type-erased boundary with `container.view(...)`. The context and
typed container must outlive the view.

`container_options` controls delimiters, key/value rendering, the entry
separator, and `max_print`. Set `max_print` for crash-time or untrusted
structures so a corrupt cycle or implausibly large size cannot produce
unbounded output.

`make_container_context` remains available for one-off container formatters.
Its required borrow is passed as `value_ref(context)` when constructing a
`remote_container_view`.

## Standard layout helpers

Use the concrete helpers when the target has a conventional layout:

| Header | Factory | Target layout |
|---|---|---|
| `remote_vector.hpp` | `make_remote_vector<T>` | data pointer, size, optional capacity |
| `remote_vector.hpp` | `make_remote_carray<T>` | fixed contiguous array |
| `remote_forward_list.hpp` | `make_remote_forward_list<T>` | head pointer and node next/data offsets |
| `remote_hash_table.hpp` | `make_remote_hash_table<Key, Value>` | bucket array with singly linked node chains |
| `remote_binary_tree.hpp` | `make_remote_binary_tree<Key, Value>` | root pointer and left/right/key/value node offsets |

Each helper accepts target pointer and size types as template parameters where
they can differ from the host. It returns an owning typed container whose
`view` method creates a `remote_container_view`.

The standard helpers build their structural field reads from
`remote_layout_query`:

* vectors use typed queries for data, size, and capacity;
* forward lists use queries for head and next pointers;
* hash tables use queries for bucket count, bucket-array pointer, bucket
  entries, and node links; and
* binary trees use queries for root, left-child, and right-child pointers.

This keeps target pointer-width conversion and offset handling consistent
across all containers. Payload addresses remain ordinary offsets because they
identify inline objects rather than stored pointer values.

```cpp
auto vector = microfmt::make_remote_vector<uint32_t>(
    remote_vector_address, data_offset, size_offset, capacity_offset);
auto view = vector.view(space, scratch);
microfmt::format_to(output, MICROFMT_STRING("values={}"), view);
```

## Custom container implementations

For a proprietary container, define a tag and specialize the matching traits:
`remote_vector_traits<Tag>`, `remote_forward_list_traits<Tag>`,
`remote_hash_table_traits<Tag>`, or `remote_binary_tree_traits<Tag>`. The
specialization declares `context_type` and static operations for that
container shape. Construct the typed wrapper with the corresponding
`make_remote_*<Tag>(address, context)` overload.

Operations receive `value_ref<const context_type>`, the address space, scratch
span, and any traversal arguments. Formatting operations also receive the
output sink and, where relevant, `container_options`. The required borrow is
non-null and const-preserving. Contexts can retain logically mutable state
such as counters or caches with `mutable` members. Read only the current node
or element into scratch; do not copy the full container into a temporary
array.

The typed wrapper retains `context_type` by value and contains no runtime
function table. Type erasure occurs only in `remote_container_view`.
See [Traits, contexts, and type erasure](traits-and-contexts.md) for the full
provider pattern and implementation guidance.

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
