<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: MIT
-->

# Inspector: symbols and diagnostics

Symbol and diagnostic views turn raw addresses into useful crash output while
keeping string and resolver storage outside formatter stack frames.

## Enumerate target images

`elf_enumerator.hpp` describes loaded ELF images through
`elf_image_enumerator_ref`. An enumerator backend locates the target image
that owns an address and reports its load base plus relevant sections, including
ARM EXIDX ranges when present.

The enumerator is type-erased and refers to caller-owned context. It can be
backed by a process loader list, a dump's mapped modules, or a fixed firmware
image table. Ensure the context remains valid for every resolver and unwinder
that uses it.

## Resolve and render symbols

`symbol_resolver_ref` dispatches address lookup to a
`symbol_resolver_traits<Tag>` implementation. A resolver produces a
`raw_resolved_symbol`; the reference derives offsets from the matched symbol
and image in `resolved_symbol_info`.

Use `remote_symbol_view` or the `symbol(...)` helper to format an address with
a resolver and a caller-owned `span<char>` for symbol-name storage:

```cpp
char name_scratch[128];
auto view = microfmt::symbol(program_counter, resolver, name_scratch);
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

Pass a bounded scratch span to every symbol or diagnostic view. The span must
remain exclusive to that formatting operation; nested symbol formatting needs
a separate region so an inner resolver cannot overwrite text that an outer
view still references.
