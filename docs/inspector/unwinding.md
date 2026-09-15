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

## Recover custom execution contexts with unwind hints

`unwind_hint_registry_ref` is the fallback for code that cannot be unwound
from an ordinary frame record. Its primary use case is a crash in a kernel
context-switch routine, scheduler trampoline, interrupt return path, or other
assembly/prologue sequence that has temporarily moved, swapped, or repurposed
the normal stack and register state.

In those regions, the value passed as `current_fp` may not identify a
conventional frame chain. A matching `unwind_hint::routine` can instead treat
it as the address of a saved-context record, read the target-specific register
slots, select the saved stack pointer and return PC, and publish the recovered
caller frame:

```cpp
bool recover_context_switch(microfmt::address_space_ref space,
                            uintptr_t context_address, uintptr_t current_pc,
                            uintptr_t &next_fp,
                            uintptr_t &next_pc) noexcept {
  saved_context context{};
  if (!space.read(context_address, context)) {
    return false;
  }

  next_fp = context.saved_stack_pointer;
  next_pc = context.saved_return_pc;
  return next_fp != 0 && next_pc != 0;
}
```

The routine receives the target address space, the current frame value, and
the current PC. It owns the target-specific interpretation: it may choose
between task and interrupt stacks, recover a register from a switch frame,
normalize an architecture-specific return address, or reject a context whose
saved registers are not valid.

Register a PC range with a custom routine in the fixed-capacity
`unwind_hint_registry_context`:

```cpp
microfmt::unwind_hint_registry_context<4> hints;
hints.add_hint({
    .pc_start = context_switch_start,
    .pc_end = context_switch_end,
    .routine = recover_context_switch,
});

microfmt::unwind_hint_registry_ref hint_registry{
    microfmt::unwind_hint_registry_tag{}, hints};
```

For a dynamic platform, bind `unwind_hint_registry_ref` to any caller-owned
object or callable exposing
`bool find_hint(uintptr_t pc, unwind_hint &out_hint) noexcept`. That resolver
can select a recovery routine from the active image, scheduler configuration,
or platform-specific PC map without heap allocation. The registry lookup is by
PC; context-sensitive register recovery belongs in the selected routine.

`chained_unwinder_context` tries EXIDX, DWARF, and frame-pointer strategies
before consulting its hint registry. Set its `current_pc` pointer to the PC
being unwound so a fallback lookup can occur. A hint is only reached after the
earlier strategies fail, and the chained unwinder requires a non-zero
`current_fp`; pass the address of the available saved-context record when no
ordinary frame pointer exists.

Hints are a recovery mechanism, not a reason to trust arbitrary context
memory. Validate every target read and return `false` when the saved stack
pointer or return PC is absent, outside the expected address range, or cannot
advance the walk.

## Available backends

| Header | Backend or supporting facility |
|---|---|
| `fp_unwinder.hpp` | Generic ABI-trait-driven frame-pointer stepper |
| `dwarf_abi.hpp` | Architecture traits and register/frame conventions |
| `dwarf_decoder.hpp` | Bounded DWARF call-frame instruction decoding |
| `unwind_hint.hpp` | PC-range hints and custom routines for context-specific frame recovery |
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
