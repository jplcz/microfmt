<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Using jplcz_microfmt

`jplcz_microfmt` is a C++17 header-only formatting library for
memory-constrained
software. It writes directly to caller-provided sinks, so formatting does not
allocate memory or throw exceptions. The core header has no third-party
dependencies.

## Add the library

For Conan 2, vcpkg, CPM.cmake, and other CMake-based dependency managers, see
the dedicated [package-manager integration guide](package-managers.md). Every
supported path provides the same `jplcz_microfmt::microfmt` CMake target.

### FetchContent and `add_subdirectory`

With CMake, use the interface target:

```cmake
include(FetchContent)
FetchContent_Declare(
    jplcz_microfmt
    GIT_REPOSITORY https://github.com/jplcz/microfmt.git
)
FetchContent_MakeAvailable(jplcz_microfmt)

target_link_libraries(my_target PRIVATE jplcz_microfmt::microfmt)
```

The equivalent direct-subdirectory form is:

```cmake
add_subdirectory(third_party/jplcz_microfmt)
target_link_libraries(my_target PRIVATE jplcz_microfmt::microfmt)
```

Embedded builds default all jplcz_microfmt development targets and install
rules to off. They do not modify `CMAKE_CXX_STANDARD`; linking
`jplcz_microfmt::microfmt` requests C++17 through target compile features.
On MSVC, the target also propagates `/Zc:preprocessor`, which is required by
the C++20 logging macros that use `__VA_OPT__`.

### The `jplcz_reloco` dependency, and reusing a parent project's own copy

`jplcz_microfmt` depends on `jplcz_reloco` (see the top-level
`CMakeLists.txt`). By default it fetches its own copy via `FetchContent`
(pinned to `JPLCZ_MICROFMT_RELOCO_GIT_TAG`, `master` unless overridden),
or uses a local checkout if `JPLCZ_MICROFMT_RELOCO_SOURCE_DIR`
(cache variable or environment variable of the same name) is set.

If a parent project has *already* brought in `jplcz_reloco` itself before
adding `jplcz_microfmt` — its own `add_subdirectory(path/to/jplcz_reloco)`,
or its own earlier `FetchContent_MakeAvailable(jplcz_reloco)` — the
`jplcz_reloco::reloco` target already exists by the time jplcz_microfmt's
`CMakeLists.txt` runs. jplcz_microfmt detects this (`if(NOT TARGET
jplcz_reloco::reloco)`) and skips fetching/declaring `jplcz_reloco` a
second time entirely, reusing the parent's target and whatever options the
parent already configured it with, instead of silently overriding them or
declaring a conflicting second `FetchContent` source for the same
dependency name:

```cmake
# Parent project's own CMakeLists.txt
add_subdirectory(third_party/jplcz_reloco)   # jplcz_reloco::reloco now exists
add_subdirectory(third_party/jplcz_microfmt) # reuses it; does not fetch its own copy

target_link_libraries(my_target PRIVATE jplcz_microfmt::microfmt)
```

This applies equally to the `FetchContent` form: as long as
`jplcz_reloco::reloco` exists (by any means) before `jplcz_microfmt`'s
`CMakeLists.txt` runs, `JPLCZ_MICROFMT_RELOCO_SOURCE_DIR`/
`JPLCZ_MICROFMT_RELOCO_GIT_REPOSITORY`/`JPLCZ_MICROFMT_RELOCO_GIT_TAG` are
all ignored, since there is nothing left for them to configure.

### Installable CMake package

Configure a standalone or `ExternalProject` build with
`JPLCZ_MICROFMT_INSTALL=ON`. Standalone builds enable it by default:

```sh
cmake -S jplcz_microfmt -B jplcz_microfmt-build \
  -DJPLCZ_MICROFMT_BUILD_TESTS=OFF \
  -DJPLCZ_MICROFMT_BUILD_EXAMPLES=OFF \
  -DJPLCZ_MICROFMT_BUILD_BENCHMARKS=OFF \
  -DJPLCZ_MICROFMT_BUILD_HEADER_CHECKS=OFF
cmake --build jplcz_microfmt-build
cmake --install jplcz_microfmt-build --prefix /opt/jplcz_microfmt
```

Consumers can then load the exported interface target:

```cmake
find_package(jplcz_microfmt CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE jplcz_microfmt::microfmt)
```

The package installs its CMake config under
`${CMAKE_INSTALL_DATADIR}/cmake/jplcz_microfmt` (typically
`share/cmake/jplcz_microfmt`) rather than under `lib/`, since
`jplcz_microfmt` is a header-only INTERFACE library with no
architecture-specific binaries to match. Set `CMAKE_PREFIX_PATH` to the
chosen installation prefix, or set `jplcz_microfmt_DIR` directly to that
directory.

The install also copies `README.md`, `LICENSE`, and the full `docs/` guide
tree under `${CMAKE_INSTALL_DOCDIR}` (typically
`share/doc/jplcz_microfmt/`), preserving the source layout so the
cross-links between guides keep resolving. No Doxygen or HTML build step is
involved. Set `JPLCZ_MICROFMT_BUILD_MANPAGES=ON` to additionally render each
top-level guide to a `man(7)` page with `pandoc` (`jplcz_microfmt(7)` for
`README.md`, `jplcz_microfmt-usage(7)` for `docs/usage.md`, and so on),
installed under `${CMAKE_INSTALL_MANDIR}/man7`; the configure step fails if
`pandoc` is not found while this option is enabled.

Standalone builds also enable CPack, producing archive, Debian, and RPM
packages with the same layout as `cmake --install`. See
[CPack](package-managers.md#cpack) for details.

Alternatively, add `include/` to the include path and include only the headers
for the facilities in use. The core API is in:

```cpp
#include <microfmt/microfmt.hpp>
```

Optional formatters and sinks each have their own headers. For example,
`<microfmt/formatters/ranges.hpp>` enables range formatting and
`<microfmt/sinks/stdio.hpp>` enables terminal and file output.

## Format into a bounded buffer

`format<N>` returns a `buffer_sink<N>` with inline storage. Its output is a
non-owning `microfmt::string_view`, and excess output is safely truncated.

```cpp
#include <microfmt/microfmt.hpp>

const auto message = microfmt::format<64>(
    "sensor={}, value=0x{:04X}", 7, 0x2a);

microfmt::string_view text = message.view();
// "sensor=7, value=0x002A"
```

Use `span_sink` to write to storage owned by the caller:

```cpp
char storage[32];
microfmt::span_sink output{microfmt::span<char>{storage, sizeof(storage)}};

microfmt::format_to(output.as_sink(), "state={}", "ready");
// output.view() is "state=ready"
```

`span_sink` and `buffer_sink` do not append a null terminator. Use their
`view()` results as `microfmt::string_view`, or use `c_string_sink<N>` when a
null-terminated buffer is required.

See [Hardened containers and views](hardened-containers.md) for checked access,
non-trapping `try_*` operations, assertion handling, and the explicit security
opt-out.

## Stream to a sink

The type-erased `sink` consists of an optional context pointer and a write
callback. It is suitable for device drivers, protocol writers, and logging
backends.

```cpp
void uart_write(void *, microfmt::string_view chunk) noexcept {
  for (char ch : chunk) {
    uart_putc(ch);
  }
}

microfmt::sink uart{nullptr, uart_write};
microfmt::format_to(uart, "temperature={} C\n", 24);
```

The core also supplies output-iterator, callback, counting, null, fixed
buffer, and span-backed sinks. Use `ring_buffer_sink<Capacity>` from
`<microfmt/sinks/ring_buffer_sink.hpp>` when only the most recent diagnostic
output should be retained; its capacity must be a non-zero power of two.
`<microfmt/sinks/memory_buffer.hpp>` adds `memory_buffer<InlineCapacity>`, a
dynamically growing, `std::back_inserter`-compatible buffer that starts on
the stack and falls back to an allocator (`reloco::allocator_ref`, defaulting
to `reloco::default_allocator()`) once its inline capacity is exceeded.
`<microfmt/sinks/c_string_span_sink.hpp>` adds `c_string_span_sink`, which
writes into a caller-owned `span<char>` (or `std::span<char>`) and always
keeps the result null-terminated, reserving one byte for the terminator.
`<microfmt/sinks/sbuf_sink.hpp>` adds `sbuf_sink<SBufT>`, a zero-allocation
adapter over a FreeBSD-style `struct sbuf *` that appends through
`sbuf_bcat`; it has no hard dependency on `<sys/sbuf.h>` unless instantiated.

## Compile-time format strings

The runtime format string used throughout this guide is the default choice
for most call sites. Wrap a string literal in `MICROFMT_STRING(...)` only
when a specific call site needs the compile-time formatting path:

```cpp
microfmt::format_to(output.as_sink(),
                    MICROFMT_STRING("id={}, flags={:08b}"), id, flags);
```

This parses the replacement fields during constant evaluation and uses
unrolled argument dispatch, which avoids the stack-resident argument-pointer
array and indirect thunks that the runtime path uses. Reserve it for hot
paths and embedded targets with strict stack budgets, where the same literal
and argument types are formatted repeatedly; each distinct format string and
argument-type combination generates its own unrolled code, so overusing it —
many distinct literals, or the same literal instantiated over many
argument-type combinations, as happens inside a template that is itself
instantiated for many types — grows code size for little benefit. Prefer
runtime format strings everywhere else, including when the format text is
not known at compile time:

```cpp
microfmt::string_view format_from_configuration = "id={}";
microfmt::format_to(output.as_sink(), format_from_configuration, id);
```

Core replacement fields consume arguments in order by default. Numeric
positions can select or reuse zero-based arguments explicitly:

```cpp
microfmt::format_to(output.as_sink(), "{1} then {0}", first, second);
microfmt::format_to(output.as_sink(), "{1:08x} {0}", name, value);
```

Use `{}` for the default representation, `{:x}` or `{:X}` for hexadecimal
integers, and `{:04x}` for a zero-padded hexadecimal width. Positional fields
use the same specifiers after `:`, as in `{2:04x}`. Automatic and numeric
fields may coexist; numeric fields do not advance the automatic argument
index. Literal braces are written as `{{` and `}}`. Named arguments and
dynamic width or precision are not supported. Individual formatter headers
may define additional specifiers; the API reference lists them.

## Format into a container

`format_as<TargetContainer>(fmt, args...)` formats into a newly constructed
container (typically `std::string`) instead of a caller-supplied sink,
appending through `std::back_inserter`:

```cpp
std::string message = microfmt::format_as<std::string>(
    "sensor={}, value=0x{:04X}", 7, 0x2a);
```

It accepts both runtime format strings and `MICROFMT_STRING(...)` literals.

## Type-erased arguments

`make_format_args(args...)` captures pointers to `args` into a small,
stack-only, type-erased `format_args` view; the object it returns must not
outlive the full expression that consumes it, since it borrows its arguments
and, when built from temporaries, itself. Pass it to `vformat_args_to` (or
`vformat_args_as<TargetContainer>`) to format without instantiating a
formatting function per argument-type combination — useful for reducing code
size behind a non-template boundary such as a logging facade:

```cpp
void log_line(microfmt::string_view fmt, microfmt::format_args args) {
  microfmt::vformat_args_to(output.as_sink(), fmt, args);
}

int count = 7;
log_line("count={}", microfmt::make_format_args(count));
```

## Add formatters and views

Specialize `microfmt::formatter<T>` to format an application type. `parse`
receives the replacement-field specifier and `format` writes directly to the
sink.

```cpp
struct point {
  int x;
  int y;
};

template <> struct microfmt::formatter<point> {
  constexpr void parse(microfmt::format_parse_context &) noexcept {}

  void format(const point &value, const microfmt::sink &out) const noexcept {
    microfmt::format_to(out, "({}, {})", value.x, value.y);
  }
};
```

The optional formatter headers add zero-allocation views for ranges, binary
values, escaped strings, fixed-point values, UUIDs, tuples, maps, bitfields,
hex dumps, source locations, and more. Include the corresponding header before
formatting that view.

## Logging

Include `<microfmt/log/logger.hpp>` to create a fixed-capacity structured
logger. Attach one or more `log_sink` objects that consume `log_msg` records.

```cpp
#include <microfmt/log/logger.hpp>

microfmt::log::logger logger{"telemetry", my_log_sink};
logger.set_level(microfmt::log::level::info);
logger.info("sensor={} online", 7);
```

`trace`, `debug`, `info`, `warn`, `error`, and `critical` have both runtime
and `MICROFMT_STRING` overloads. The macros in `<microfmt/log/macros.hpp>`
accept the format string as-is and use runtime formatting by default, the
same as the member functions above:

```cpp
MICROFMT_LOGGER_INFO(logger, "sensor={} online", 7);
```

Wrap the format string in `MICROFMT_STRING(...)` at a call site that
explicitly needs compile-time formatting:

```cpp
MICROFMT_LOGGER_INFO(logger, MICROFMT_STRING("sensor={} online"), 7);
```

Define `MICROFMT_DEFAULT_LOGGER` before including `macros.hpp` to enable
`MICROFMT_LOG_INFO(...)` and the related default-logger macros.

## Platform and language support

The core supports C++17 and later. C++20 and C++23 APIs are exposed only when
the associated standard-library facilities are available. Public-header
compatibility is checked by the project in C++17, C++20, and, when supported
by the compiler, C++23.

In low-level application code, prefer `microfmt::array`, `microfmt::span`,
`microfmt::string_view`, and `microfmt::expected` over equivalent standard
types. Keep standard containers at platform and third-party boundaries, then
convert to hardened views before parsing or traversal. See
[Hardened containers and views](hardened-containers.md).

Keep allocation-sensitive code on the core sink APIs. Some optional bridges,
such as PMR containers and `{fmt}` compatibility, intentionally use
ecosystem types and may not have the same allocation properties.
