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
cross-links between guides keep resolving. No HTML build step is
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

### Descriptor, socket and debugger sinks

Real output usually has to leave the process: into a file, over the network,
or into a debugger. These sinks wrap the matching OS call so a signal, a full
socket buffer or a long message does not silently lose or corrupt output.
Each sink borrows what it writes to (it never opens or closes it), counts
bytes it had to drop, and has a `*_log_sink` adapter that renders each record
as one `[logger] payload` message for the logging API.

| Sink | Header | Use it when |
|---|---|---|
| `posix_fd_sink` | `<microfmt/sinks/posix_fd_sink.hpp>` | Writing to a file, pipe or tty with `write(2)` and you need `EINTR`/short-write handling or `EAGAIN` policy (`drop` or `retry`). The stateless `fd_sink(fd)` helper in `stdio.hpp` is the minimal version without retries or accounting. |
| `socket_sink` | `<microfmt/sinks/socket_sink.hpp>` | Sending to a connected UDP (`datagram` mode: one `write` is one packet) or TCP (`stream` mode: short sends are continued) socket. Uses `MSG_NOSIGNAL` so a closed peer does not raise `SIGPIPE`. |
| `win_debug_sink<Capacity>` | `<microfmt/sinks/win_debug_sink.hpp>` | Showing output in the Visual Studio Output window or DebugView on Windows via `OutputDebugStringA`; long data is split into NUL-terminated chunks. |

```cpp
const int fd = ::open("app.log", O_WRONLY | O_APPEND | O_CREAT, 0644);
microfmt::posix_fd_sink file(fd);
microfmt::format_to(file.as_sink(), "boot {}\n", 1);

microfmt::log::posix_fd_log_sink<> log_to_file(file);
auto logger_sink = log_to_file.as_sink(); // a microfmt::log::log_sink
```

Every sink also accepts a custom write function (for example a test double),
which is how the default OS call is replaced in unit tests. The default
constructors that use the OS call exist only where that API does
(`posix_fd_sink` and `socket_sink` off Windows, `win_debug_sink` on Windows).

### Queued, rotating and journald sinks

Three more sinks solve common production problems:

- **Slow output must not stall the caller.** `async_queue_sink<Capacity>` is a
  fixed-size single-producer/single-consumer (SPSC: one writing thread, one
  reading thread) byte queue. The hot thread formats into it with no locks or
  allocation; another thread calls `drain(target, max_bytes)` to move the bytes
  to a slow sink such as a file. When the queue is full the *newest* bytes are
  dropped and counted in `dropped_bytes()`, so logging never blocks.
  `Capacity` must be a power of two. Unlike `ring_buffer_sink`, which
  overwrites old data for crash dumps, this queue keeps data until drained.
- **Log files must not grow forever.** `rotating_file_sink<PathCapacity>`
  writes to `app.log` and, before a write that would exceed `max_bytes`, renames
  `app.log` to `app.log.1`, `app.log.1` to `app.log.2` and so on, keeping
  `max_backups` generations (the oldest is overwritten). A single write larger
  than the limit goes whole into a fresh file. It is not thread-safe; put it
  behind `async_queue_sink` or a lock when several threads log.
- **Linux services should log to the journal with structured fields.**
  `microfmt::log::journald_native_sink<Capacity>` sends each record as one
  datagram in journald's native protocol (`PRIORITY=`, `SYSLOG_IDENTIFIER=`,
  `MESSAGE=`), so the severity is filterable with `journalctl -p`. Messages
  containing newlines use the protocol's length-prefixed binary form. Long
  messages are truncated to `Capacity`; failed sends are counted in
  `dropped_records()`. Create the socket with `journald_native_open()`; the sink
  borrows it.

```cpp
microfmt::async_queue_sink<4096> queue;
microfmt::format_to(queue.as_sink(), "event {}\n", 1); // hot thread

microfmt::rotating_file_sink<> file("app.log", 1 << 20, 3); // 1 MiB, 3 backups
queue.drain(file.as_sink());                                 // writer thread
```

Both `rotating_file_sink` (through `rotating_file_ops`) and
`journald_native_sink` (through a send function) accept replacement functions
so they can be tested without touching the real file system or journal.

## Named arguments

Positional fields (`{0}`, `{1}`) break when a message is reworded, reordered
or translated, because the argument order in the code no longer matches the
text. Give an argument a name with `microfmt::arg` and refer to it as
`{name}` instead; the order then no longer matters:

```cpp
using microfmt::arg;

microfmt::format_to(output.as_sink(), "{user} logged in from {ip:15}",
                    arg("user", name), arg("ip", address));

// Positional and named arguments can be mixed; `{0}` is the first argument.
microfmt::format_to(output.as_sink(), "{0} -> {target}", id, arg("target", path));
```

A name is a letter or underscore followed by letters, digits or underscores.
Everything after the `:` is the normal format specification, so every
existing formatter works unchanged. The name is looked up when the message is
formatted, so the format text may come from a configuration file or a
translation table.

Rules worth knowing:

- An unknown name prints `{MISSING}`, like an out-of-range index, because
  formatting never throws.
- If two arguments share a name, the first one wins.
- A bare `{}` counts every argument in order, named ones included; a `{name}`
  field does not advance that counter.
- `arg` only borrows the value, so it is safe inside one `format_to` call. If
  you build the arguments first with `make_format_args`, keep the result of
  `arg(...)` in a variable (the compiler warns about a dangling temporary).
- `MICROFMT_STRING` still validates the text, but a call that contains a named
  argument is formatted by the runtime engine, since names are resolved at
  runtime.

### Building the argument list at runtime

`format_to` needs the arguments written out in the source code. When they come
from data instead (the fields of a log record, or the entries a configuration
file asks for), fill a `dynamic_format_args<Capacity>` and pass it to
`vformat_args_to`. It never allocates: `push` returns `false` and adds nothing
once `Capacity` is reached.

```cpp
const microfmt::string_view user = "ann";
const int count = 3;

microfmt::dynamic_format_args<8> args;
const bool ok = args.push("user", user) && args.push("count", count);

microfmt::vformat_args_to(output.as_sink(), format_from_configuration, args);
```

The builder only borrows the values, so each one must outlive the formatting
call. Pushing a temporary (`args.push("n", 3)`) is therefore rejected at
compile time; store the value in a variable first. `args.push(value)` adds an
unnamed argument that is reachable by position only.

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

If you want the compile-time *validation* of `MICROFMT_STRING` but not its
per-literal code, define `MICROFMT_COMPACT_STRING` before including microfmt.
`MICROFMT_STRING("...")` then checks the string during constant evaluation
(an invalid string is a compile error) and yields a plain
`microfmt::string_view`, so every call site shares the runtime `vformat_to`
path. You give up the unrolled fast path in exchange for smaller binaries.
Code that calls `.get()` on the result of `MICROFMT_STRING` must not use this
mode.

Core replacement fields consume arguments in order by default. Numeric
positions can select or reuse zero-based arguments explicitly:

```cpp
microfmt::format_to(output.as_sink(), "{1} then {0}", first, second);
microfmt::format_to(output.as_sink(), "{1:08x} {0}", name, value);
```

Use `{}` for the default representation, `{:x}` or `{:X}` for hexadecimal
integers, `{:b}` for binary, and `{:04x}` or `{:08b}` for a zero-padded width.
Integer specs are `[#][0][width][b|x|X]`, where `#` adds the `0b`/`0x`/`0X`
prefix and the `#` and `0` flags may appear in either order. Positional fields
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

### Compiling log calls out entirely

Run-time filtering (`logger.set_level(...)`) still leaves every format string,
file name and function name in the binary. On size-constrained targets
(kernels, firmware) set the compile-time level `MICROFMT_ACTIVE_LEVEL` so the
macros below it become `((void)0)`: arguments are not evaluated and no string
literal is emitted. This works like spdlog's `SPDLOG_ACTIVE_LEVEL`.

```cpp
// Must be defined before <microfmt/log/macros.hpp> (or pass it on the
// compiler command line, e.g. -DMICROFMT_ACTIVE_LEVEL=3, or configure with
// -DJPLCZ_MICROFMT_ACTIVE_LEVEL=warn). Constants: MICROFMT_LEVEL_TRACE (0),
// DEBUG, INFO, WARN, ERROR, CRITICAL, OFF (6). Default: TRACE (keep all).
#define MICROFMT_ACTIVE_LEVEL MICROFMT_LEVEL_WARN
#include <microfmt/log/macros.hpp>

MICROFMT_LOGGER_INFO(logger, "boot step {}", expensive()); // no-op: not evaluated, string not emitted
MICROFMT_LOGGER_ERROR(logger, "boot failed: {}", code);    // kept; still subject to logger.should_log()
```

Only the `MICROFMT_LOGGER_<LEVEL>` and `MICROFMT_LOG_<LEVEL>` macros are
affected; calling `logger.info(...)` directly or `MICROFMT_LOGGER_LOG` with a
run-time level is never removed. Unused-variable warnings for values only
used in disabled log calls are the caller's concern.

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
