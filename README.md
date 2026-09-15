<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# microfmt

<img src="docs/microfmt-logo.svg" alt="microfmt logo" width="128">

`microfmt` is a header-only C++ formatting and diagnostics library for
resource-constrained software. Its core writes directly to bounded buffers,
callbacks, device transports, and other sinks without exceptions, RTTI,
virtual dispatch, or heap allocation.

It is designed for embedded firmware, RTOS tasks, interrupt paths, crash
handlers, kernel-adjacent code, and diagnostic tools that need predictable
storage and explicit failure handling.

## Guides

Start with the guide that matches what you are building:

| Guide | Covers |
|---|---|
| [Using microfmt](docs/usage.md) | Installation, core formatting, sinks, compile-time strings, custom formatters, and logging |
| [Hardened containers](docs/hardened-containers.md) | Checked views and results, non-trapping access, assertion handling, and explicit security opt-out |
| [Formatter guide](docs/formatters.md) | Binary and diagnostic values, ranges, time, units, protocols, structured output, and presentation |
| [Inspector framework](docs/inspector.md) | Remote memory, objects, containers, symbols, registers, and stack unwinding |
| [Writing low-stack renderers](docs/renderer-guide.md) | Caller-owned scratch storage and small formatter/view design |
| [Developing microfmt](docs/development.md) | Builds, tests, warning policy, public-header checks, and contribution constraints |

The [`examples/`](examples) directory contains runnable programs for the core
API and nearly every optional formatter, sink, and inspector subsystem.

## Core formatting

The core API provides sequential `{}` replacement fields, integer widths and
bases, escaped braces, custom `formatter<T>` specializations, and runtime or
compile-time format strings.

```cpp
#include <microfmt/microfmt.hpp>

const auto message = microfmt::format<64>(
    MICROFMT_STRING("sensor={}, value=0x{:04X}"), 7, 0x2a);

// message.view() == "sensor=7, value=0x002A"
```

`format<N>` stores at most `N` characters in inline storage. Formatting to a
bounded sink truncates excess output instead of allocating or throwing.

Use `MICROFMT_STRING(...)` for literals. It validates and parses the format
string during constant evaluation and selects unrolled argument dispatch.
Runtime `microfmt::string_view` formats remain available for
configuration-driven text.

The core supports C++17 and later. Features that depend on newer standard
library APIs are enabled only when available.

## Sinks and output routing

Formatting always targets a small type-erased `microfmt::sink`. Built-in
adapters cover:

- Inline and caller-owned bounded buffers.
- Null-terminated buffers and output iterators.
- Callbacks, byte counting, and discarded output.
- `FILE*`, stdout, stderr, and POSIX file descriptors.
- Circular trace buffers that retain the newest output.
- Tee, prefix, transform, and output-limiting sinks.
- Container and PMR-backed output when dynamic storage is intentional.

```cpp
#include <microfmt/microfmt.hpp>

void uart_write(void *, microfmt::string_view chunk) noexcept {
  for (char ch : chunk)
    uart_putc(ch);
}

microfmt::sink uart{nullptr, uart_write};
microfmt::format_to(
    uart, MICROFMT_STRING("temperature={} C\n"), 24);
```

The sink model lets the same formatter stream to a UART, trace buffer, crash
record, terminal, or application logger without first constructing a string.

## Structured logging

`microfmt` includes fixed-capacity structured logging with severity filtering,
explicit logger instances, compile-time logging macros, and pluggable sinks.

Available backends include stdio, syslog, Android logcat, systemd journal,
Tizen DLOG, ring buffers, and user-defined consumers.

```cpp
#include <microfmt/log/logger.hpp>

microfmt::log::logger logger{"telemetry", my_log_sink};
logger.set_level(microfmt::log::level::info);
logger.info(MICROFMT_STRING("sensor={} online"), 7);
```

See [Using microfmt](docs/usage.md#logging) for logger setup and macro usage.

## Formatter library

Optional headers extend the core with lightweight values and non-owning views.
Include only the formatter families an application uses.

### Values and diagnostics

- Binary, hexadecimal, base64, hashes, and escaped byte or string views.
- Fixed-point and floating-point values, scaled units, and byte sizes.
- Pointers, address offsets, memory ranges, bitfields, and register layouts.
- Hex dumps, UUIDs, semantic versions, network addresses, and variants.
- Chrono, POSIX time, FreeBSD binary time, and source locations.

### Ranges and structured values

- Joined and filtered ranges.
- Tuples, pairs, maps, grids, vectors, and matrices.
- Optional and expected values.
- Boost.Describe reflected enums and objects.

### Protocols and generated output

- CAN, I2C, and SPI transaction diagnostics.
- Streaming JSON and CBOR writers.
- ANSI styling, text padding, quoting, case conversion, and truncation.
- Markdown headings, lists, code blocks, block quotes, and tables.
- A lightweight `{fmt}` compatibility bridge.

```cpp
#include <microfmt/formatters/binary.hpp>
#include <microfmt/formatters/ranges.hpp>

const uint8_t bytes[] = {0x12, 0x34, 0x56};
const auto line = microfmt::format<96>(
    MICROFMT_STRING("bytes=[{}], flags={}"),
    microfmt::join(bytes, ":"),
    microfmt::bin_prefixed(uint8_t{0x2a}, true));

// bytes=[18:52:86], flags=0b0010_1010
```

The [formatter guide](docs/formatters.md) links to focused references for each
family and documents formatter-specific specifiers.

## Inspector framework

The inspector subsystem formats data outside the normal local C++ object
graph: another process, a target device, a crash dump, kernel memory, or
ABI-defined unwind data.

Its main features include:

- Type-erased, fault-aware address-space transports.
- Foreign strings, remote objects, and explicit 32-bit compatibility types.
- Remote vectors, forward lists, hash tables, binary trees, and smart
  pointers.
- Virtual-to-physical address translation and memory classification.
- Bounded memory scanning with caller-owned scratch storage.
- ELF image enumeration, symbol resolution, demangling, and fault
  diagnostics.
- Architecture register catalogs and type-erased register access.
- Frame-pointer, DWARF-style, hybrid, chained, and ARM EHABI EXIDX unwinding.

Inspector views keep traversal state and scratch storage explicit. Remote reads
return failure instead of directly dereferencing untrusted target addresses,
and container traversal is bounded by caller-selected limits.

```cpp
#include <microfmt/inspector/address_space.hpp>

auto space =
    microfmt::address_space_ref::make<microfmt::local_space_tag>();

alignas(int) std::byte scratch[sizeof(int)]{};
int value = 42;
microfmt::remote_ref<int> remote{
    reinterpret_cast<uintptr_t>(&value), space, scratch};

microfmt::format_to(output, MICROFMT_STRING("{}"), remote);
```

See the [inspector framework guide](docs/inspector.md) for transports, remote
layouts, symbolization, architecture support, and unwinding.

## Add microfmt

Use the CMake interface target:

```cmake
include(FetchContent)
FetchContent_Declare(
    microfmt
    GIT_REPOSITORY https://github.com/jplcz/microfmt.git
)
FetchContent_MakeAvailable(microfmt)

target_link_libraries(my_target PRIVATE microfmt::microfmt)
```

Alternatively, add `include/` to the compiler include path:

```cpp
#include <microfmt/microfmt.hpp>
```

The core has no required third-party dependencies. Optional integrations are
enabled only when their corresponding headers or platform libraries are
available.

## Build the repository

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Strict GCC and Clang warnings are enabled by default for project targets.
Public headers are compiled under C++17, C++20, and C++23 when supported by
the active compiler.

See [Developing microfmt](docs/development.md) for build options, focused test
commands, stack-usage checks, and project constraints.

## License

See [LICENSE](LICENSE).
