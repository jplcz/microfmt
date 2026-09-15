<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: MIT
-->

# Inspector: ARM EXIDX unwinding

ARM EHABI unwind metadata is stored in `.ARM.exidx` entries and, for some
functions, `.ARM.extab` bytecode streams. The ARM inspector headers decode that
metadata incrementally to recover the caller frame without allocating.

## Components

| Header | Responsibility |
|---|---|
| `arm_exidx_search.hpp` | Decode PREL31 values and locate the matching EXIDX entry for a program counter |
| `arm_exidx_decoder.hpp` | Interpret compact inline EXIDX bytecode |
| `arm_extab_decoder.hpp` | Decode EXIDX entries that refer to extended tables |
| `arm_extab_stream.hpp` | Execute bounded extended-table bytecode streams |
| `arm_exidx_unwinder.hpp` | Adapt EXIDX lookup and decoding to `frame_unwinder_ref` |

`elf_enumerator_ref` locates the loaded image that owns a return address and
its EXIDX range. The unwinder then finds the nearest entry, decodes the inline
or extended bytecode, and derives the next frame state.

## Use an off-stack holder

`arm_exidx_unwinder_holder` owns the substantial register-state and image
scratch required by the EXIDX backend. It is deliberately non-copyable and
non-movable so its context's interior pointers remain stable.

```cpp
microfmt::arm_exidx_unwinder_holder holder{space, image_enumerator};
auto unwinder = holder.make_ref();
```

Store the holder in a crash-reporting context, debugger session, or other
dedicated owner. Do not create it as a formatter-local object: ARM register
state is intentionally kept off the formatter stack.

## Target requirements

The address-space backend must read target 32-bit words safely. The image
enumerator must report correct image load addresses and `.ARM.exidx` bounds.
The stack must be readable at the current frame pointer, and the target's
frame layout must be compatible with the selected unwinder path.

EXIDX information is optional in many binaries. The EXIDX backend can be
combined with frame-pointer, hint-based, or other unwinders through the
generic chaining facilities. Configure a fallback order that reflects the
metadata guaranteed by the target build.

## Failure behavior

Invalid alignment, unreadable stack slots, missing required holder storage, or
an invalid caller frame cause the step to fail. Callers should stop the walk or
try an explicitly configured fallback; they must not continue with
uninitialized frame state.
