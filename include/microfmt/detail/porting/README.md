# `detail/porting/`

This directory is microfmt's fixed-path plug-in point for every
platform-specific "`_CUSTOM`" backend (see each header's own top-level
doc comment and `microfmt_config.hpp` for the full rationale), mirroring
reloco's own `detail/porting/` layer (`reloco/detail/porting/README.md`).

| `_CUSTOM` macro                           | Fixed include                              | Scaffold (ships here)                |
|--------------------------------------------|----------------------------------------------|----------------------------------------|
| `MICROFMT_DEFAULT_LOG_SINK_BACKEND_CUSTOM` | `detail/porting/default_log_sink.hpp`       | `default_log_sink.template.hpp`       |

Only the `*.template.hpp` files above ship in this repository -- they are
documentation-only scaffolds (each one sketches, loosely, what a FreeBSD
kernel port might look like; none is compiled or exercised by this
repository's own build/test suite) and are never `#include`d by anything. The
corresponding real header (`default_log_sink.hpp`, ...) is `#include`d
**unconditionally**, at a fixed path, by microfmt's own header that owns
that customization point (`microfmt/log/logger.hpp`, ...) whenever its
matching `_CUSTOM` macro is defined -- right at the exact point the
built-in backend would otherwise have defined the same entry point. That
means:

- Correctness never depends on *where else* the application/kernel
  happens to include its replacement from (the previous "included by the
  application through the normal path" convention required the
  replacement to already be visible before the *first* microfmt header
  that references it -- effectively an include-order requirement that
  grows fragile as more microfmt headers use the same entry point
  transitively). With a fixed include path, whichever microfmt header
  reaches (say) `microfmt::log::detail::built_in_default_logger()` first
  triggers the same one `#include` at the same spot, with the exact same
  result, regardless of anything else.
- You do not have to place your files here by hand: set the
  `JPLCZ_MICROFMT_PORTING_HEADERS` CMake variable (see the top-level
  `CMakeLists.txt`) to a directory containing `microfmt_user_config.hpp`
  and/or any of the fixed-include file names above before configuring microfmt
  (top-level build, or via
  `add_subdirectory`/`FetchContent`); the build copies whichever of those
  files exist there into this directory (in the build tree, then
  installed alongside microfmt's own headers) and bakes the matching
  `MICROFMT_*_BACKEND_CUSTOM` macro into a generated
  `detail/porting/microfmt_generated_config.hpp` placed right alongside them (see
  `cmake/generated_porting_config.hpp.in`), rather than an INTERFACE
  `target_compile_definitions` on the `jplcz_microfmt` CMake target -- so
  the override takes effect for every consumer of the plain `include/`
  tree, including one that never links `jplcz_microfmt` as an actual
  CMake target (a hand-copied `include/` directory, or a downstream
  `find_package(jplcz_microfmt)` consumer using a different build system
  than the one that produced the install tree). `microfmt_config.hpp`
  picks this generated file up automatically via a plain
  `__has_include`-guarded `#include`, so it is a no-op when
  `JPLCZ_MICROFMT_PORTING_HEADERS` was never set (the file is simply
  never generated). See reloco's own `detail/porting/README.md` for the
  identical mechanism this mirrors.
- If you are not using CMake (or prefer to manage it yourself), place
  `microfmt_user_config.hpp` and any completed non-`.template` backend
  scaffolds directly in this directory. The user configuration is included
  automatically when present; define backend macros directly when no
  generated configuration header is present.

A file placed here **never replaces** any of microfmt's own headers --
each real microfmt header (`log/logger.hpp`, ...) always exists and is
always the one an application/kernel `#include`s; a `detail/porting/*.hpp`
file only ever supplies the *contents* that header `#include`s in place of
its own built-in backend, and only once the matching `_CUSTOM` macro opts
out of that built-in backend in the first place.

## `default_log_sink.hpp`

Owned by `microfmt/log/logger.hpp`'s built-in `MICROFMT_ENABLE_DEFAULT_LOGGER`
process-wide default logger (see `docs/porting.md`). The built-in backend
is an ANSI-colorized `stdout_color_sink<256>`, appropriate for a hosted
console application but not for e.g. a daemon that should log through the
OS's native facility (syslog, the systemd journal, Android logcat -- see
`microfmt/sinks/{syslog,systemd,android_log}_sink.hpp`, which an
application can already wire in by hand via `set_default_logger()`
without this customization point) or an RTOS/bare-metal target with
neither stdout nor any of those facilities at all.

Must define, in namespace `microfmt::log::detail`, the whole entry point
`built_in_default_logger()` (not merely a sink helper), giving the port
full control over the resulting `logger` too -- its name, sink
count/capacity, locking policy:

```cpp
// detail/porting/default_log_sink.hpp (this exact path/name)
namespace microfmt::log::detail {
inline logger &built_in_default_logger() noexcept { ... }
}
```

returning a reference to a `microfmt::log::logger` (see
`microfmt/log/logger.hpp`) usable for the whole lifetime of the program --
typically a function-local `static logger` instance constructed from a
function-local `static` sink's own `.as_sink()` (see
`default_log_sink.template.hpp`), exactly like the built-in
`built_in_default_logger()`/`default_console_sink()`/
`stdout_color_sink<256>` trio this replaces.
