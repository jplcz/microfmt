<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Porting microfmt

`microfmt/microfmt_config.hpp` is the single build-time customization entry
point for every optional feature macro (`RELOCO_KERNEL`,
`MICROFMT_USE_SYSTEM_ERROR`, `RELOCO_DISABLE_ASSERT*`,
the Boost integration switches, the default-logger switches, etc.). It is
included first, before anything else, by `microfmt/detail/compat.hpp`, so it
is always processed before any header applies its own default for one of
these macros.

Do not edit `microfmt_config.hpp` directly to set overrides. Instead, define
`MICROFMT_CONFIG` (e.g. `-DMICROFMT_CONFIG=1`) to opt in to including a header
named `microfmt_user_config.hpp`, which must be reachable on the compiler's
include search path (e.g. in an application-owned include directory listed
*before* microfmt's own `include/` directory). When `MICROFMT_CONFIG` is
defined, `microfmt_user_config.hpp` is included first, so every `#define` it
contains takes precedence over both this file and the individual
`#ifndef`-guarded defaults each feature header applies on its own. A compiler
`-D` flag works exactly the same way for any individual macro and can be used
instead of, or together with, `microfmt_user_config.hpp`.

```cpp
// microfmt_user_config.hpp, in a directory added to the include path ahead
// of microfmt's own include/ directory.
#pragma once
#define MICROFMT_ENABLE_DEFAULT_LOGGER
```

Build with `-DMICROFMT_CONFIG=1` (or an equivalent build-system define) so the
header above is picked up; without it, `microfmt_user_config.hpp` is never
included even if present on the include path.

Every microfmt header that participates in the normal C++ include graph
reaches `microfmt_config.hpp` transitively — through `compat.hpp` — before
checking any `MICROFMT_*` customization macro, so this mechanism covers the
whole library uniformly, not just `compat.hpp` itself.
`microfmt/inspector/unwind_hint_asm.h` is the one header consumed outside that
graph (it is included directly from `.S` assembler files); it also includes
`microfmt_config.hpp` explicitly so its own `MICROFMT_UNWIND_HINT_POINTER_SIZE`
macro is customizable through the same `MICROFMT_CONFIG` mechanism.

`microfmt/detail/compat.hpp` is the central compatibility layer for compiler,
language-standard, standard-library, and platform feature detection. Porting
work should add detection there and consume the resulting `MICROFMT_*` macro
elsewhere instead of testing compiler or platform predefined macros directly.

The header exposes the normalized `RELOCO_CXX_STANDARD` value and the
boolean `RELOCO_CXX11`, `RELOCO_CXX14`, `RELOCO_CXX17`,
`RELOCO_CXX20`, and `RELOCO_CXX23` macros. It also provides compatibility
attributes and the `MICROFMT_HAS_*` feature macros used by conditional APIs.
These definitions are intended to isolate newer language features while the
library is progressively adapted for C++11.


## Override trap and unreachable operations

Platforms may define `RELOCO_TRAP()` and `RELOCO_UNREACHABLE()` before the
first microfmt header is included:

```cpp
#define RELOCO_TRAP() platform_debug_trap()
#define RELOCO_UNREACHABLE() platform_unreachable()
#include <microfmt/microfmt.hpp>
```

An overridden `RELOCO_UNREACHABLE()` is assumed to provide a real
unreachable-code intrinsic. If the platform has no such operation, define it
as a no-op and set `RELOCO_HAS_UNREACHABLE` to `0`:

```cpp
#define RELOCO_UNREACHABLE() ((void)0)
#define RELOCO_HAS_UNREACHABLE 0
#include <microfmt/microfmt.hpp>
```

`RELOCO_HAS_UNREACHABLE` controls whether disabled assertions may use the
operation as an optimizer hint. Setting it to `0` keeps those assertions as
complete no-ops. Overrides must be consistent in every translation unit and
must appear before any microfmt header because the compatibility header uses
`#pragma once`.

`RELOCO_TRAP()` must not return. The default implementation uses the
compiler debug trap when available and falls back to `std::abort()`.

See [Bare-metal hardware sinks](bare-metal.md) for the PL011 UART and ARM
semihosting `microfmt::sink` adapters shipped under `microfmt/hw/`.

## errno formatting on kernel/bare-metal targets

`microfmt/formatters/errno.hpp` formats `microfmt::posix_errno` (produced by
`format_errno(int)` or `current_errno()`) as a human-readable message plus the
raw numeric code, e.g. `No such file or directory (os:2)`. Message rendering
is delegated to `microfmt::detail::write_errno_string(const sink &, int)`,
which has two selectable implementations:

| Macro                          | Behavior                                                                 |
| ------------------------------- | ------------------------------------------------------------------------ |
| `MICROFMT_USE_SYSTEM_ERROR=1`   | Uses `std::error_code`/`std::system_category()` to render the message.  |
| `MICROFMT_USE_SYSTEM_ERROR=0` (default) | Uses `strerror_s`/`strerror_r`/`strerror`, selected per-platform, avoiding `<string>`/`<system_error>`. |

Both are host/user-space implementations and are compiled only when
`RELOCO_KERNEL` is *not* defined. Set `MICROFMT_USE_SYSTEM_ERROR` before the
first inclusion of `errno.hpp` if the default choice does not fit the target.

For kernel or freestanding targets, define `RELOCO_KERNEL` before including
`errno.hpp`. This suppresses every host-only include (`<string>`,
`<system_error>`, `<cstring>`) and forward-declares
`microfmt::detail::write_errno_string` instead of providing a definition. The
port must supply exactly one definition, linked against the target's own
error-message facility (e.g. kernel log tables or a custom `strerror`
equivalent):

```cpp
#define RELOCO_KERNEL
#include <microfmt/formatters/errno.hpp>

namespace microfmt::detail {
void write_errno_string(const sink &out, int value) noexcept {
  out.write(platform_errno_to_string(value));
}
} // namespace microfmt::detail
```

## Assert failures on kernel/bare-metal targets

`microfmt/detail/assert.hpp` reports `RELOCO_ASSERT`/`RELOCO_DEBUG_ASSERT`
failures. On hosted platforms this goes through a replaceable
`assert_handler_t` (see `set_assert_handler`), whose built-in
`default_assert_handler` formats the failing expression, file, line, and
message with `std::fprintf(stderr, ...)` — this requires a hosted C library
and is unsuitable for kernel/freestanding builds.

Defining `RELOCO_KERNEL` before including `assert.hpp` removes that runtime,
function-pointer-based indirection (and the `<cstdio>` include) entirely.
Instead, `RELOCO_ASSERT`/`RELOCO_DEBUG_ASSERT` expand to call a
port-supplied `RELOCO_KERNEL_PANIC(expression, file, line, message)` macro
directly, immediately followed by `RELOCO_TRAP()`. The port must define this
macro (a "kernel panic trait") before the first inclusion of `assert.hpp`;
omitting it is a compile error:

- Preferred: delegate to a printf-like kernel panic/log function, passing the
  four fields through as format arguments.
- Fallback: write the four fields out individually as raw strings (e.g. via a
  UART or semihosting `microfmt::sink`) when no printf-like facility exists.

```cpp
#define RELOCO_KERNEL
#define RELOCO_KERNEL_PANIC(expression, file, line, message)                 \
  kernel_panicf("[ASSERT] %s at %s:%d (%s)\n", expression, file, line, message)
#include <microfmt/detail/assert.hpp>
```

`RELOCO_KERNEL_PANIC` need not return (most kernel panic facilities halt or
reset the system), but if it does, the subsequent `RELOCO_TRAP()` call still
applies.

`RELOCO_DISABLE_ASSERT_STDIO` remains available for hosted targets that want
to silently drop assert diagnostics without a kernel panic facility; it has no
effect when `RELOCO_KERNEL` is defined.

## Adding a platform or compiler

Keep compiler-specific syntax inside `compat.hpp`. Prefer standard feature-test
macros after conditionally including the corresponding standard header, and
provide a conservative `0` or empty fallback when a feature is unavailable.
Do not infer standard-library support from the language version alone.
