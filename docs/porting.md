<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Porting microfmt

`microfmt/detail/compat.hpp` is the central compatibility layer for compiler,
language-standard, standard-library, and platform feature detection. Porting
work should add detection there and consume the resulting `MICROFMT_*` macro
elsewhere instead of testing compiler or platform predefined macros directly.

The header exposes the normalized `MICROFMT_CXX_STANDARD` value and the
boolean `MICROFMT_CXX11`, `MICROFMT_CXX14`, `MICROFMT_CXX17`,
`MICROFMT_CXX20`, and `MICROFMT_CXX23` macros. It also provides compatibility
attributes and the `MICROFMT_HAS_*` feature macros used by conditional APIs.
These definitions are intended to isolate newer language features while the
library is progressively adapted for C++11.

## Override trap and unreachable operations

Platforms may define `MICROFMT_TRAP()` and `MICROFMT_UNREACHABLE()` before the
first microfmt header is included:

```cpp
#define MICROFMT_TRAP() platform_debug_trap()
#define MICROFMT_UNREACHABLE() platform_unreachable()
#include <microfmt/microfmt.hpp>
```

An overridden `MICROFMT_UNREACHABLE()` is assumed to provide a real
unreachable-code intrinsic. If the platform has no such operation, define it
as a no-op and set `MICROFMT_HAS_UNREACHABLE` to `0`:

```cpp
#define MICROFMT_UNREACHABLE() ((void)0)
#define MICROFMT_HAS_UNREACHABLE 0
#include <microfmt/microfmt.hpp>
```

`MICROFMT_HAS_UNREACHABLE` controls whether disabled assertions may use the
operation as an optimizer hint. Setting it to `0` keeps those assertions as
complete no-ops. Overrides must be consistent in every translation unit and
must appear before any microfmt header because the compatibility header uses
`#pragma once`.

`MICROFMT_TRAP()` must not return. The default implementation uses the
compiler debug trap when available and falls back to `std::abort()`.

## Thread-local storage (TLS) provider

`microfmt/detail/tls_provider.hpp` supplies `microfmt::detail::tls_provider<T,
Tag>`, a tag-differentiated per-thread (or per-task) storage cell used by
freestanding-friendly features such as the fallible address space's
signal-safe recovery context. Storage is uniquely keyed by both the stored
type `T` and a unique `Tag` type, so unrelated features never collide even if
they happen to store the same `T`:

```cpp
struct my_feature_tls_tag {};
using my_tls = microfmt::detail::tls_provider<int, my_feature_tls_tag>;

my_tls::set(42);
int value = my_tls::get(); // 42, reference-returning get() is also mutable in place
```

`get()` returns `T &` (default-constructed to `T{}` before the first `set()`
call on a given thread/task) and `set(T value)` moves a new value into the
slot. A partial specialization for pointer types, `tls_provider<T *, Tag>`,
stores `T *` directly and defaults to `nullptr`.

### Selecting a storage model

The active implementation is selected at compile time with the
`MICROFMT_TLS_MODEL` macro, which must be defined (if at all) before the first
inclusion of `tls_provider.hpp`:

| Macro value                        | Model                                                          |
| ----------------------------------- | --------------------------------------------------------------- |
| `MICROFMT_TLS_MODEL_THREAD_LOCAL` (default) | Standard C++ `thread_local` storage.                     |
| `MICROFMT_TLS_MODEL_PTHREAD`        | POSIX `pthread_key_t`-based storage with lazy heap allocation and automatic cleanup on thread exit. |
| `MICROFMT_TLS_MODEL_SINGLE`         | Single global instance shared by every caller; suitable for single-threaded or bare-metal targets without per-task storage. |
| `MICROFMT_TLS_MODEL_WIN32`          | Win32 Fiber Local Storage (FLS), with automatic cleanup when a fiber/thread exits. |
| `MICROFMT_TLS_MODEL_OS`             | Stub for custom OS or bare-metal task-control-block mappings; the platform must provide its own `tls_provider<T, Tag>::get()`/`set()` definitions. |

```cpp
#define MICROFMT_TLS_MODEL MICROFMT_TLS_MODEL_PTHREAD
#include <microfmt/detail/tls_provider.hpp>
```

For `MICROFMT_TLS_MODEL_OS`, `tls_provider<T, Tag>` is declared but not
defined; provide an explicit specialization per `T`/`Tag` pair (or a matching
partial specialization) that implements `get()`/`set()` against the target
RTOS's task-local storage facilities.

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
`MICROFMT_KERNEL` is *not* defined. Set `MICROFMT_USE_SYSTEM_ERROR` before the
first inclusion of `errno.hpp` if the default choice does not fit the target.

For kernel or freestanding targets, define `MICROFMT_KERNEL` before including
`errno.hpp`. This suppresses every host-only include (`<string>`,
`<system_error>`, `<cstring>`) and forward-declares
`microfmt::detail::write_errno_string` instead of providing a definition. The
port must supply exactly one definition, linked against the target's own
error-message facility (e.g. kernel log tables or a custom `strerror`
equivalent):

```cpp
#define MICROFMT_KERNEL
#include <microfmt/formatters/errno.hpp>

namespace microfmt::detail {
void write_errno_string(const sink &out, int value) noexcept {
  out.write(platform_errno_to_string(value));
}
} // namespace microfmt::detail
```

## Assert failures on kernel/bare-metal targets

`microfmt/detail/assert.hpp` reports `MICROFMT_ASSERT`/`MICROFMT_DEBUG_ASSERT`
failures. On hosted platforms this goes through a replaceable
`assert_handler_t` (see `set_assert_handler`), whose built-in
`default_assert_handler` formats the failing expression, file, line, and
message with `std::fprintf(stderr, ...)` — this requires a hosted C library
and is unsuitable for kernel/freestanding builds.

Defining `MICROFMT_KERNEL` before including `assert.hpp` removes that runtime,
function-pointer-based indirection (and the `<cstdio>` include) entirely.
Instead, `MICROFMT_ASSERT`/`MICROFMT_DEBUG_ASSERT` expand to call a
port-supplied `MICROFMT_KERNEL_PANIC(expression, file, line, message)` macro
directly, immediately followed by `MICROFMT_TRAP()`. The port must define this
macro (a "kernel panic trait") before the first inclusion of `assert.hpp`;
omitting it is a compile error:

- Preferred: delegate to a printf-like kernel panic/log function, passing the
  four fields through as format arguments.
- Fallback: write the four fields out individually as raw strings (e.g. via a
  UART or semihosting `microfmt::sink`) when no printf-like facility exists.

```cpp
#define MICROFMT_KERNEL
#define MICROFMT_KERNEL_PANIC(expression, file, line, message)                 \
  kernel_panicf("[ASSERT] %s at %s:%d (%s)\n", expression, file, line, message)
#include <microfmt/detail/assert.hpp>
```

`MICROFMT_KERNEL_PANIC` need not return (most kernel panic facilities halt or
reset the system), but if it does, the subsequent `MICROFMT_TRAP()` call still
applies.

`MICROFMT_DISABLE_ASSERT_STDIO` remains available for hosted targets that want
to silently drop assert diagnostics without a kernel panic facility; it has no
effect when `MICROFMT_KERNEL` is defined.

## Adding a platform or compiler

Keep compiler-specific syntax inside `compat.hpp`. Prefer standard feature-test
macros after conditionally including the corresponding standard header, and
provide a conservative `0` or empty fallback when a feature is unavailable.
Do not infer standard-library support from the language version alone.
