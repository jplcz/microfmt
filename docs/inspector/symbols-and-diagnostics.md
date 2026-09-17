<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Inspector: symbols and diagnostics

Symbol and diagnostic views turn raw addresses into useful crash output while
keeping string and resolver storage outside formatter stack frames.

## Enumerate target images

`elf_enumerator.hpp` describes loaded ELF images through
`elf_image_enumerator_ref`. An enumerator backend locates the target image
that owns an address and reports its load base plus relevant sections, including
ARM EXIDX ranges when present.

The erased reference derives its context type and operations from
`elf_image_enumerator_traits<Tag>`. `elf_image_enumerator<Tag>` retains that
context by value and exposes `ref()`; direct references can instead borrow a
caller-owned context. A backend can represent a process loader list, dump
modules, or a fixed firmware image table.

## Resolve and render symbols

`symbol_resolver_ref` dispatches address lookup to a
`symbol_resolver_traits<Tag>` implementation. A resolver produces a
`raw_resolved_symbol`; the reference derives offsets from the matched symbol
and image in `resolved_symbol_info`.

Use `remote_symbol_view` or `make_remote_symbol(...)` to format an address with
a resolver and a caller-owned `symbol_resolution_context`. The context keeps
both symbol-name scratch and resolver temporaries outside formatter stack
frames:

```cpp
char name_scratch[128];
microfmt::symbol_resolution_context symbol_context{name_scratch};
auto view =
    microfmt::make_remote_symbol(program_counter, resolver, symbol_context);
microfmt::format_to(output, MICROFMT_STRING("pc={:#}"), view);
```

The `#` form requests detailed symbol output where supported. If resolution
fails, the view still renders an address or fault-oriented representation, so
diagnostic output remains useful without symbols.

## Demangling

`demangle.hpp` provides bounded C++ symbol presentation through
`demangle_view` and the associated helpers. Demangling is part of rendering
policy: keep its target text and working storage in the view or resolver
context rather than creating a large formatter-local buffer.

Use a resolver's demangling option only when its additional code and output are
appropriate for the diagnostic path. Stripped images and non-C++ symbols
should continue to render cleanly.

## Diagnostic views

`remote_diagnostics.hpp` combines address, symbol, and memory-read results
into fault-aware diagnostic text. It is useful for return addresses, function
references, and failures discovered by a remote renderer.

`remote_diag_ref<T>::load()` returns
`expected<T *, remote_load_error>`, using the same null-address,
scratch-size/alignment, invalid-space, and read-failure diagnostics as
`remote_ref<T>`. Its formatter converts a failed load into symbol-aware fault
text, while direct callers can inspect the error and choose their own recovery
policy.

Pass a `symbol_resolution_context` to every symbol or diagnostic view. Its
bounded scratch span and temporary records must remain exclusive to that
formatting operation; nested symbol formatting needs a separate context so an
inner resolver cannot overwrite text that an outer view still references.
