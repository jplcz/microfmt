<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Inspector framework

The inspector framework renders data that lives outside the normal C++ object
graph: another process, a crash dump, a target device, a kernel address space,
or an ABI-defined unwind table. It provides bounded, zero-allocation views over
that data and formats them through the usual `microfmt::sink` interface.

It is designed for diagnostic paths where target memory may be incomplete or
unreadable. Address-space and remote-object loading operations report typed
failures through `microfmt::expected`; predicates, iteration callbacks, and
other control-flow-only operations continue to use `bool`. Formatters emit
fault markers instead of dereferencing target pointers directly.

## Start here

An inspection flow has four layers:

1. An `address_space_ref` reads target bytes through a user-defined transport.
2. A view describes a remote object, container, symbol, or stack frame and
   receives caller-owned scratch storage.
3. A type-erased resolver, enumerator, or unwinder supplies platform-specific
   behavior where needed.
4. A regular `microfmt::formatter` streams the view to a sink.

The view is deliberately small. Keep substantial scratch buffers in the
debugger, task, crash handler, or other long-lived owner, then pass them to the
view as `span<char>` or `span<std::byte>`. Do not put target snapshots,
decoder state, or symbol buffers in formatter-local arrays. See the
[low-stack renderer guide](renderer-guide.md) for the general pattern.

## Subsystem guides

| Guide | Use it for | Primary headers |
|---|---|---|
| [Traits, contexts, and type erasure](inspector/traits-and-contexts.md) | Implementing provider traits, choosing context ownership, crossing `*_ref` boundaries, const-correct borrowing, and testing custom providers | `address_space.hpp`, `register_context.hpp`, `remote_container.hpp`, `unwind_hint.hpp`, `elf_enumerator.hpp` |
| [Memory and remote objects](inspector/memory-and-objects.md) | Target transports, address translation and classification, remote page-table walking, bounded memory scanning, byte-level memory diffing (local and remote), foreign and C++ string objects, reusable layout queries, ABI-width pointer wrappers, and reflected structures | `address_space.hpp`, `address_translator.hpp`, `remote_page_table_walker.hpp`, `memory_classifier.hpp`, `memory_diff.hpp`, `remote_memory_diff.hpp`, `memory_scanner.hpp`, `compat32.hpp`, `foreign_string_view.hpp`, `remote_layout_accessor.hpp`, `remote_basic_string.hpp`, `remote_object.hpp`, `remote_smart_ptr.hpp` |
| [Memory pattern scanners](inspector/memory-pattern-scanners.md) | Exact and masked signatures, scalar range searches, dependent field predicates, scratch sizing, custom scanner traits, and error handling | `memory_pattern_scanner.hpp`, `advanced_scanners.hpp` |
| [Remote containers](inspector/containers.md) | Vectors, linked lists, hash tables, binary trees, and custom container layouts | `remote_container.hpp`, `remote_vector.hpp`, `remote_forward_list.hpp`, `remote_hash_table.hpp`, `remote_binary_tree.hpp` |
| [Metadata maps](inspector/metadata.md) | Fixed-capacity heterogeneous properties, type-erased generators, formatting, and lifetime requirements | `metadata_map.hpp`, `concrete_metadata_map.hpp`, `value_ref.hpp` |
| [Tasks and threads](inspector/tasks-and-threads.md) | Scheduler descriptors, thread enumeration, register contexts, and unified metadata views | `task.hpp`, `thread.hpp`, `metadata_map.hpp` |
| [GDB Remote Serial Protocol](inspector/gdb-protocol.md) | Request and response payloads, register layouts and data, target XML, binary escaping, checksums, and streaming framing | `gdb_encoders.hpp`, `gdb_decoders.hpp`, `gdb_register_array.hpp`, `register_xml_printer.hpp`, `gdb_stream.hpp` |
| [Symbols and diagnostics](inspector/symbols-and-diagnostics.md) | ELF image discovery, symbol resolution, demangling, and diagnostic views | `elf_enumerator.hpp`, `symbol_resolver.hpp`, `demangle.hpp`, `remote_diagnostics.hpp` |
| [Architectures and registers](inspector/architectures-and-registers.md) | Register contexts, architecture catalogs, system and timer registers, address candidates, and register rendering | `register_context.hpp`, `dwarf_registers.hpp`, `dwarf_abi.hpp`, `register_view.hpp` |
| [Generic unwinding](inspector/unwinding.md) | Frame cursors, backtraces, frame-pointer walkers, custom-context recovery hints, and chained unwinders | `frame_pointer.hpp`, `fp_unwinder.hpp`, `dwarf_abi.hpp`, `dwarf_decoder.hpp`, `unwind_hint.hpp`, `chained_unwinder.hpp`, `hybrid_unwinder.hpp`, `exception_frame.hpp` |
| [ARM EXIDX unwinding](inspector/arm-unwinding.md) | ARM EHABI `.ARM.exidx` and `.ARM.extab` decoding | `arm_exidx_decoder.hpp`, `arm_exidx_search.hpp`, `arm_exidx_unwinder.hpp`, `arm_extab_decoder.hpp`, `arm_extab_stream.hpp` |

## Minimal remote-object example

The built-in local transport is useful for tests and tools that inspect their
own address space. Production tools normally specialize
`address_space_traits<Tag>` for their memory reader instead.

```cpp
#include <microfmt/inspector/address_space.hpp>
#include <microfmt/inspector/remote_object.hpp>

struct registers {
  uint32_t status;
  uint32_t control;
};

MICROFMT_REMOTE_STRUCT_BEGIN(registers)
  MICROFMT_REMOTE_FIELD(status, uint32_t)
  MICROFMT_REMOTE_FIELD(control, uint32_t)
MICROFMT_REMOTE_STRUCT_END()

registers local{0x12, 0x34};
std::byte scratch[sizeof(registers) + 32]{};
auto space = microfmt::address_space_ref{microfmt::local_space_tag{}};
auto view = microfmt::remote_object_view{
    reinterpret_cast<uintptr_t>(&local), space, microfmt::type_tag<registers>{},
    scratch};

microfmt::format_to(output, MICROFMT_STRING("{}"), view);
// { status: 18, control: 52 }
```

The address-space, view, and scratch-buffer owners must outlive formatting.
For untrusted target data, choose a transport that validates target ranges;
`local_space_tag` performs ordinary local reads and is not a safe remote-memory
reader.

## Common rules

* Treat all target addresses and lengths as untrusted input.
* Use fixed-capacity, caller-owned scratch buffers and check every
  `microfmt::expected` read or load result before accessing its value.
* Preserve pointer width explicitly with the compatibility wrappers when host
  and target ABIs differ.
* Bound traversal with `container_options::max_print` and use fault-aware
  container or symbol views in crash paths.
* Keep resolver, enumerator, and unwinder contexts alive while their
  type-erased references or views are in use.
* Use `MICROFMT_STRING(...)` for literal diagnostic formats.

The `examples/` directory contains complete runnable demonstrations, including
`address_translator_demo.cpp`, `memory_classifier_demo.cpp`,
`memory_diff_demo.cpp`, `remote_memory_diff_demo.cpp`,
`memory_scanner_demo.cpp`, `memory_pattern_scanner_demo.cpp`,
`advanced_scanners_demo.cpp`, `remote_struct_demo.cpp`,
`task_thread_demo.cpp`,
`remote_vector_context_demo.cpp`, `remote_hash_table_demo.cpp`,
`resolver_demo.cpp`, and `unwinder_demo.cpp`.
