<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# jplcz_microfmt

<img src="docs/microfmt-logo.svg" alt="microfmt logo" width="128">

`jplcz_microfmt` is a header-only C++ formatting and diagnostics library for
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
| [Using jplcz_microfmt](docs/usage.md) | Installation, core formatting, sinks, compile-time strings, custom formatters, and logging |
| [Package-manager integration](docs/package-managers.md) | Conan 2, vcpkg overlays, CPM.cmake, CPack packaging, and CMake-based dependency managers |
| [Hardened containers](docs/hardened-containers.md) | Checked views and results, non-trapping access, assertion handling, and explicit security opt-out |
| [Formatter guide](docs/formatters.md) | Binary and diagnostic values, ranges, time, units, protocols, structured output, and presentation |
| [Inspector framework](docs/inspector.md) | Remote memory, objects, containers, symbols, registers, and stack unwinding |
| [Writing low-stack renderers](docs/renderer-guide.md) | Caller-owned scratch storage and small formatter/view design |
| [Extending microfmt](docs/extending.md) | Copy-paste templates for custom formatters, sinks, and inspector providers |
| [Lifetime safety](docs/lifetime-safety.md) | Borrowed values and pointers, lifetime annotations, and compiler diagnostics |
| [Bare-metal hardware sinks](docs/bare-metal.md) | PL011 UART and ARM semihosting `microfmt::sink` adapters |
| [GDB pretty printers](docs/gdb-pretty-printers.md) | Formatting microfmt sinks, buffers, and the logger in GDB: source, auto-load, or embed |
| [Developing jplcz_microfmt](docs/development.md) | Builds, tests, warning policy, public-header checks, and contribution constraints |

The [`examples/`](examples) directory contains runnable programs for the core
API and nearly every optional formatter, sink, and inspector subsystem,
including a standalone bare-metal QEMU `virt` example
([`examples/bare_metal/qemu-virt`](examples/bare_metal/qemu-virt)) that boots
without an OS or libc and formats directly to a PL011 UART.

For firmware, kernel-mode, crash-path, and other security-sensitive code,
prefer `microfmt::array`, `microfmt::span`, `microfmt::string_view`, and
`microfmt::expected` over their standard-library counterparts. Their checked
operations remain hardened in release builds and borrowing accessors reject
unsafe temporaries. Use standard containers at explicit interoperability or
dynamic-allocation boundaries where their behavior is intentional.

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

`MICROFMT_STRING(...)` validates and parses a literal format string during
constant evaluation and selects unrolled argument dispatch. It is well suited
to hot paths, but each distinct format string and argument-type combination
generates its own unrolled code, so overuse can grow code size; see
[Choosing `MICROFMT_STRING` carefully](#choosing-microfmt_string-carefully).
Runtime `microfmt::string_view` formats share one compact core loop and
remain the default choice for less latency-sensitive call sites and
configuration-driven text.

The core supports C++17 and later. Features that depend on newer standard
library APIs are enabled only when available.

## Design rationale: a small, type-erased core

`format_to` and `format<N>` are thin templates: for each call they build a
`const void *` pointer per argument and a matching compile-time table of
per-type formatting thunks, then hand both to a single, non-template
`vformat_to`. That function is the only place that walks the format string,
matches `{}`/`{N}`/`{N:spec}` fields, and dispatches through the thunk table
to call each argument's `formatter<T>` (or a user's `formatter<T>`
specialization) through a type-erased function pointer.

Because `vformat_to` itself is never instantiated per `Args...`, it does not
get duplicated for every distinct call-site signature the way a fully
templated recursive formatter would. Template bloat is confined to the small,
constexpr-friendly glue that builds the pointer/thunk arrays; the loop that
actually parses the format string and writes to the `microfmt::sink` compiles
once and is shared by every call in the binary. This keeps code size and
instruction-cache pressure predictable on embedded targets, where duplicated
per-signature formatting loops are a common source of bloat.

This is also why the library favors non-owning **views** (`string_view`,
`span`, `join`, and similar range/value wrappers) over owning containers in
its argument and formatter APIs. A view only needs to carry a pointer/size and
a lightweight `formatter<T>` that forwards into the shared core loop; it does
not pull in container-specific template machinery for the common case where a
value has no format specifier and is simply written to the sink directly. For
the usual "print this value with no spec" path, the per-argument thunk is a
minimal, near-direct call, and only formatters that need to parse a spec
string do additional work — so straightforward, specifier-free formatting
stays close to a plain, unrolled write rather than paying for a general
parsing/templating layer it doesn't use.

### Choosing `MICROFMT_STRING` carefully

`MICROFMT_STRING(...)` trades the small, shared `vformat_to` core for a fully
compile-time-unrolled call: it parses the format string at compile time and
generates a dedicated `unrolled_format_impl` instantiation, with zero
indirect thunks and zero stack-resident argument-pointer array, for every
distinct `(format string, Args...)` combination. That is faster and avoids
the type-erased dispatch entirely, but each unique call site pays for its own
unrolled instantiation instead of sharing the one non-template loop that
runtime `microfmt::string_view` formats reuse.

Prefer `MICROFMT_STRING` on hot paths, small argument counts, and a modest
number of distinct format strings, where the unrolled code is a net win.
Avoid it for large fan-out call sites — many distinct format strings, or the
same format string instantiated over many different argument-type
combinations (e.g. via heavily templated call wrappers) — since each variant
adds its own unrolled code instead of collapsing into the shared core. In
those cases, prefer runtime `microfmt::string_view` formats, or reserve
`MICROFMT_STRING` for the specific call sites where its speed benefit clearly
outweighs the added code size.

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
- Boost.Asio `mutable_buffer` and `streambuf` adapters, including a
  coroutine helper for zero-allocation async formatting and writing.
- Bare-metal device sinks, such as the ARM PL011 UART controller.

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
Tizen DLOG, ring buffers, and user-defined consumers. The logger's name is
forwarded as the native tag/identifier to backends that support one (Android
logcat, systemd journal, Tizen DLOG), falling back to each sink's own
configured default when unset. Advanced callers can also dispatch a
caller-built `log_msg` directly to a logger's sinks, bypassing formatting.

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
and object loads return typed `microfmt::expected` errors instead of directly
dereferencing untrusted target addresses, and container traversal is bounded by
caller-selected limits.

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

## Add jplcz_microfmt

Native Conan 2, vcpkg, CPM.cmake, and CPack integration is documented in the
[package-manager guide](docs/package-managers.md).

### FetchContent or `add_subdirectory`

Use the CMake interface target:

```cmake
include(FetchContent)
FetchContent_Declare(
    jplcz_microfmt
    GIT_REPOSITORY https://github.com/jplcz/microfmt.git
)
FetchContent_MakeAvailable(jplcz_microfmt)

target_link_libraries(my_target PRIVATE jplcz_microfmt::microfmt)
```

Direct `add_subdirectory` usage exposes the same target:

```cmake
add_subdirectory(third_party/jplcz_microfmt)
target_link_libraries(my_target PRIVATE jplcz_microfmt::microfmt)
```

When embedded as a subdirectory, jplcz_microfmt does not enable its tests,
examples, benchmarks, header checks, strict warnings, or install rules by default. It
also does not change the parent project's global C++ standard. The interface
target requires C++17.

### Installed package and `ExternalProject`

Standalone builds enable `JPLCZ_MICROFMT_INSTALL` by default:

```sh
cmake -S jplcz_microfmt -B jplcz_microfmt-build \
  -DJPLCZ_MICROFMT_BUILD_TESTS=OFF \
  -DJPLCZ_MICROFMT_BUILD_EXAMPLES=OFF \
  -DJPLCZ_MICROFMT_BUILD_BENCHMARKS=OFF \
  -DJPLCZ_MICROFMT_BUILD_HEADER_CHECKS=OFF
cmake --build jplcz_microfmt-build
cmake --install jplcz_microfmt-build --prefix /opt/jplcz_microfmt
```

The installation contains the public headers and a CMake config package:

```cmake
find_package(jplcz_microfmt CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE jplcz_microfmt::microfmt)
```

An `ExternalProject_Add` dependency should pass its install prefix and disable
development-only targets:

```cmake
include(ExternalProject)
ExternalProject_Add(
    microfmt_external
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/jplcz_microfmt"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DJPLCZ_MICROFMT_INSTALL=ON
        -DJPLCZ_MICROFMT_BUILD_TESTS=OFF
        -DJPLCZ_MICROFMT_BUILD_EXAMPLES=OFF
        -DJPLCZ_MICROFMT_BUILD_BENCHMARKS=OFF
        -DJPLCZ_MICROFMT_BUILD_HEADER_CHECKS=OFF
)
```

Set `CMAKE_PREFIX_PATH` or `jplcz_microfmt_DIR` to the installed package
directory when configuring a separate consuming project.

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
the active compiler. Clang 24's opt-in `-Wunsafe-buffer-usage` analysis uses a
ratcheted diagnostic baseline:

```bash
./scripts/check-unsafe-buffer-usage.sh
```

See [Developing jplcz_microfmt](docs/development.md) for build options, focused test
commands, stack-usage checks, and project constraints. Toolchain maintainers
should also read [Porting jplcz_microfmt](docs/porting.md) for compatibility macros,
platform hook overrides, and the tag-differentiated TLS provider. See
[Lifetime safety and `value_ref`](docs/lifetime-safety.md) for non-owning
reference rules and compiler annotations.

## License

See [LICENSE](LICENSE).
