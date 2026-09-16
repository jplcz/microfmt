<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Inspector: tasks and threads

`task_info` and `thread_info` are non-owning descriptors for scheduler and
crash-diagnostic data. They combine fixed telemetry fields with type-erased
handles for address spaces, register contexts, image enumeration, custom
metadata, and thread traversal.

## Task descriptors

`task_info` records the team ID, name, scheduling state, priority, CPU ticks,
and memory usage. Its optional handles provide:

| Member | Purpose |
|---|---|
| `space` | Reads memory belonging to the task |
| `elf_enumerator` | Enumerates executable images and unwind metadata |
| `extended_metadata_fn` / `extended_metadata_ctx` | Appends platform-specific properties |
| `thread_next_fn` / `thread_next_ctx` | Enumerates the task's threads |

Call `get_next_thread` until it returns `false`. The callback context owns all
iteration state and must outlive the traversal.

## Thread descriptors

`thread_info` records the thread ID, name, priority, execution state, CPU
ticks, stack capacity, and stack high-water usage. Its `regs` handle provides
type-erased access to the saved architecture register context.

Both descriptors are views. They do not own strings, callback contexts,
address spaces, register state, or enumerators. Keep every referenced object
alive while the descriptor is used.

## Unified metadata views

`make_metadata_view` combines each descriptor's core fields with its optional
extended metadata callback:

```cpp
microfmt::task_info::metadata_state state;
auto metadata = task.make_metadata_view(state);

char scratch[64];
microfmt::md::writer document(output);
microfmt::md::write_metadata_table(
    document, metadata, scratch, "Task Metadata");
```

The caller owns the small `metadata_state`; it must outlive the returned
`metadata_map`. Iteration consumes the state. Call `make_metadata_view` again
to reset it before another traversal. Creating a metadata view from a
temporary task or thread descriptor is rejected.

Core task metadata is emitted before extended task metadata. Core thread
metadata includes both `stack_size_bytes` and `stack_usage_bytes`, followed by
extended thread metadata. Extended callbacks use the same
`metadata_next_fn_t` contract documented in
[Inspector: metadata maps](metadata.md).

## Example

`examples/task_thread_demo.cpp` creates a task with two threads, enumerates
them through the callback wrapper, and writes task and thread metadata tables
directly to standard output.
