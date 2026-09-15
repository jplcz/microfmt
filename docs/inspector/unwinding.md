<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: MIT
-->

# Inspector: generic stack unwinding

The generic unwinding subsystem walks saved frame state through a small
type-erased `frame_unwinder_ref`, then formats frame records and backtraces.
It is intended for bounded crash reporting, not as a replacement for a full
debugger's unwinder.

## The unwinder interface

Specialize `frame_unwinder_traits<Tag>` with a `context_type` and a `step`
operation. `step` receives a current frame pointer and returns the caller frame
pointer and program counter.

```cpp
struct platform_unwinder_tag {};

template <> struct microfmt::frame_unwinder_traits<platform_unwinder_tag> {
  using context_type = platform_unwinder_context;

  static bool step(const void *context, uintptr_t current_fp,
                   uintptr_t &next_fp, uintptr_t &next_pc) noexcept;
};
```

Create a `frame_unwinder_ref` from the tag and its context. The context is
borrowed, so it must outlive the iterator or backtrace view.

## Walk and render frames

`frame_pointer_iterator` performs a forward-only walk beginning with an
initial frame pointer and program counter. It yields `stack_frame` records with
the depth, frame pointer, and program counter. Use the backtrace views in
`frame_pointer.hpp` to stream a bounded stack trace, optionally through a
`symbol_resolver_ref`.

Set a practical maximum frame count. An unwinder must reject null, unaligned,
non-advancing, and otherwise invalid caller frame data. These checks prevent a
corrupt frame chain from becoming an infinite traversal.

## Available backends

| Header | Backend or supporting facility |
|---|---|
| `fp_unwinder.hpp` | Generic ABI-trait-driven frame-pointer stepper |
| `dwarf_abi.hpp` | Architecture traits and register/frame conventions |
| `dwarf_decoder.hpp` | Bounded DWARF call-frame instruction decoding |
| `unwind_hint.hpp` | Architecture or platform hints used to recover frames |
| `chained_unwinder.hpp` | Ordered fallback between multiple unwinders |
| `hybrid_unwinder.hpp` | Combined strategies and rendered backtrace support |
| `exception_frame.hpp` | Exception/trap frame decoding and trap summaries |

Use the simplest backend that matches the target's guarantees. A
frame-pointer walker is often appropriate for firmware built with stable frame
pointers. Use richer hint, DWARF, or chained approaches only when the target
image and crash context provide the required metadata.

## Scratch and resolver ownership

Frame walking itself is incremental, but symbolization and exception decoding
need caller-owned work buffers. Keep those buffers with the crash-reporting
session or a dedicated unwinder holder. Do not allocate register snapshots,
symbol strings, or decoder tables in `formatter<backtrace_view>::format`.

When several fallback unwinders share a scratch buffer, sequence them so one
operation no longer needs its transient text before the next reuses the
buffer. For nested formatting, partition the backing storage explicitly.
