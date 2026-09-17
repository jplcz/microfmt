<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Inspector: generic stack unwinding

The generic unwinding subsystem walks saved frame state through a small
type-erased `frame_unwinder_ref`, then formats frame records and backtraces.
It is intended for bounded crash reporting, not as a replacement for a full
debugger's unwinder.

## The unwinder interface

Specialize `frame_unwinder_traits<Tag>` with a `context_type` and a `step`
operation. `step` receives a `register_context_ref` and returns the caller
frame pointer and program counter. The backend reads whichever architecture
registers it needs and may update the context while applying unwind rules.

```cpp
struct platform_unwinder_tag {};

template <> struct microfmt::frame_unwinder_traits<platform_unwinder_tag> {
  using context_type = platform_unwinder_context;

  static bool step(const void *context,
                   microfmt::register_context_ref registers,
                   uintptr_t &next_fp, uintptr_t &next_pc) noexcept;
};
```

Create a `frame_unwinder_ref` from the tag and its context. The context is
borrowed, so it must outlive the iterator or backtrace view.

## Walk and render frames

`frame_pointer_iterator` performs a forward-only walk beginning with a
register context, initial frame pointer, and initial program counter. It yields
`stack_frame` records with the depth, frame pointer, and program counter. Use
the backtrace views in `frame_pointer.hpp` to stream a bounded stack trace,
optionally through a `symbol_resolver_ref`.

Set a practical maximum frame count. An unwinder must reject null, unaligned,
non-advancing, and otherwise invalid caller frame data. These checks prevent a
corrupt frame chain from becoming an infinite traversal.

## Recover custom execution contexts with unwind hints

`unwind_hint_registry_ref` is the fallback for code that cannot be unwound
from an ordinary frame record. Its primary use case is a crash in a kernel
context-switch routine, scheduler trampoline, interrupt return path, or other
assembly/prologue sequence that has temporarily moved, swapped, or repurposed
the normal stack and register state.

A hint routine receives the target address space and the complete mutable
`register_context_ref`:

```cpp
bool recover_context_switch(microfmt::address_space_ref space,
                            microfmt::register_context_ref registers,
                            uintptr_t &next_fp,
                            uintptr_t &next_pc) noexcept {
  uint32_t context_address = 0;
  uint32_t saved_fp = 0;
  uint32_t saved_lr = 0;

  // The context address is in a GPR rather than in the conventional FP slot.
  if (!registers.read(microfmt::dwarf::arm32::R0, context_address) ||
      !space.read_bytes(context_address, &saved_fp, sizeof(saved_fp)) ||
      !space.read_bytes(context_address + sizeof(saved_fp), &saved_lr,
                        sizeof(saved_lr)))
    return false;

  // Publish the recovered values both as frame outputs and in the mutable
  // register context for subsequent unwinder tiers/consumers.
  if (!registers.write(microfmt::dwarf::arm32::FP, saved_fp) ||
      !registers.write(microfmt::dwarf::arm32::LR, saved_lr))
    return false;

  next_fp = saved_fp;
  next_pc = static_cast<uintptr_t>(saved_lr & ~1U);
  return next_fp != 0 && next_pc != 0;
}
```

Use `read<T>()` and `write<T>()` for naturally sized register values, or
`read_raw()` and `write_raw()` for an explicitly sized register representation.
A callback can access any register exposed by the target's register context,
including GPRs used to hold saved-context pointers or return addresses. A
write may fail when the underlying context is read-only; custom routines should
propagate that failure rather than claiming a recovered frame.

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

`chained_unwinder_context<AbiTraits>` tries EXIDX, DWARF, and frame-pointer
strategies before consulting its hint registry. It reads the current PC from
the ABI's return-address register, then passes the original mutable register
context directly to the selected hint. It does not reduce the context to an
FP/PC snapshot, so non-standard hints can read and modify GPR state.

Hints are a recovery mechanism, not a reason to trust arbitrary context
memory. Validate every target read and return `false` when the saved stack
pointer or return PC is absent, outside the expected address range, or cannot
advance the walk.

## Assembly-generated hint arrays

Firmware can generate hint records from assembly without including the C++
API by including `microfmt/inspector/unwind_hint_asm.h` from a `.S` file. The
header emits only assembler macros under `__ASSEMBLER__` and defines the
pointer-sized field offsets and record size:

```asm
#include <microfmt/inspector/unwind_hint_asm.h>

.section .microfmt.unwind_hints,"a",%progbits
MICROFMT_UNWIND_HINT_TABLE_BEGIN my_unwind_hints
MICROFMT_UNWIND_HINT context_switch_start, context_switch_end, recover_context
MICROFMT_UNWIND_HINT_TABLE_END my_unwind_hints
```

The record contains `pc_start`, `pc_end`, and `routine`, in that order. Set
`MICROFMT_UNWIND_HINT_POINTER_SIZE` to `4` or `8` before including the header
when the assembler does not define `__SIZEOF_POINTER__`. The generated table
can be exposed to C++ through a platform-specific registry that copies or
iterates the records into `unwind_hint` values. Function pointers in assembly
must use the target's normal relocation and code-address conventions.

## Available backends

| Header | Backend or supporting facility |
|---|---|
| `register_context.hpp` | Type-erased register reads and writes used by unwind backends |
| `dwarf_registers.hpp` | Architecture register numbers and constexpr catalogs |
| `fp_unwinder.hpp` | Generic ABI-trait-driven frame-pointer stepper |
| `dwarf_abi.hpp` | Architecture traits and register/frame conventions |
| `dwarf_decoder.hpp` | Bounded DWARF call-frame instruction decoding |
| `unwind_hint.hpp` | PC-range hints and mutable register-aware custom recovery |
| `unwind_hint_asm.h` | Assembler-safe macros for emitting hint arrays |
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
