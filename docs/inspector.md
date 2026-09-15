<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: MIT
-->

# Inspector framework

The inspector framework renders data that lives outside the normal C++ object
graph: another process, a crash dump, a target device, a kernel address space,
or an ABI-defined unwind table. It provides bounded, zero-allocation views over
that data and formats them through the usual `microfmt::sink` interface.

It is designed for diagnostic paths where target memory may be incomplete or
unreadable. Inspector operations report failure through `bool` return values
and format fault markers instead of dereferencing target pointers directly.

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
| [Memory and remote objects](inspector/memory-and-objects.md) | Target transports, address translation and classification, bounded memory scanning, foreign strings, ABI-width pointer wrappers, and reflected structures | `address_space.hpp`, `address_translator.hpp`, `memory_classifier.hpp`, `memory_scanner.hpp`, `compat32.hpp`, `foreign_string_view.hpp`, `remote_object.hpp`, `remote_smart_ptr.hpp` |
| [Remote containers](inspector/containers.md) | Vectors, linked lists, hash tables, binary trees, and custom container layouts | `remote_container.hpp`, `remote_vector.hpp`, `remote_forward_list.hpp`, `remote_hash_table.hpp`, `remote_binary_tree.hpp` |
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
* Use fixed-capacity, caller-owned scratch buffers and check every read result.
* Preserve pointer width explicitly with the compatibility wrappers when host
  and target ABIs differ.
* Bound traversal with `container_options::max_print` and use fault-aware
  container or symbol views in crash paths.
* Keep resolver, enumerator, and unwinder contexts alive while their
  type-erased references or views are in use.
* Use `MICROFMT_STRING(...)` for literal diagnostic formats.

The `examples/` directory contains complete runnable demonstrations, including
`address_translator_demo.cpp`, `memory_classifier_demo.cpp`,
`memory_scanner_demo.cpp`, `remote_struct_demo.cpp`,
`remote_vector_context_demo.cpp`, `remote_hash_table_demo.cpp`,
`resolver_demo.cpp`, and `unwinder_demo.cpp`.
