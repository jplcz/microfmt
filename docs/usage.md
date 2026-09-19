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
    MICROFMT_STRING("sensor={}, value=0x{:04X}"), 7, 0x2a);

microfmt::string_view text = message.view();
// "sensor=7, value=0x002A"
```

Use `span_sink` to write to storage owned by the caller:

```cpp
char storage[32];
microfmt::span_sink output{microfmt::span<char>{storage, sizeof(storage)}};

microfmt::format_to(output.as_sink(), MICROFMT_STRING("state={}"), "ready");
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
microfmt::format_to(uart, MICROFMT_STRING("temperature={} C\n"), 24);
```

The core also supplies output-iterator, callback, counting, null, fixed
buffer, and span-backed sinks. Use `ring_buffer_sink<Capacity>` from
`<microfmt/sinks/ring_buffer_sink.hpp>` when only the most recent diagnostic
output should be retained; its capacity must be a non-zero power of two.

## Prefer compile-time format strings

Wrap string literals in `MICROFMT_STRING(...)` to select the compile-time
formatting path:

```cpp
microfmt::format_to(output.as_sink(),
                    MICROFMT_STRING("id={}, flags={:08b}"), id, flags);
```

This parses the replacement fields during constant evaluation and uses
unrolled argument dispatch. It is particularly useful for embedded targets
with strict stack budgets. Runtime format strings remain supported where the
format text is not known at compile time:

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
    microfmt::format_to(out, MICROFMT_STRING("({}, {})"), value.x, value.y);
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
logger.info(MICROFMT_STRING("sensor={} online"), 7);
```

`trace`, `debug`, `info`, `warn`, `error`, and `critical` have both runtime
and `MICROFMT_STRING` overloads. The macros in
`<microfmt/log/macros.hpp>` accept a string literal directly and always use
compile-time formatting:

```cpp
MICROFMT_LOGGER_INFO(logger, "sensor={} online", 7);
```

Macro format arguments must be literals; use the logger member functions or
free helpers for a runtime `microfmt::string_view` format string. Define
`MICROFMT_DEFAULT_LOGGER` before including `macros.hpp` to enable
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
