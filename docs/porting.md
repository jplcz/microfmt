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

## Adding a platform or compiler

Keep compiler-specific syntax inside `compat.hpp`. Prefer standard feature-test
macros after conditionally including the corresponding standard header, and
provide a conservative `0` or empty fallback when a feature is unavailable.
Do not infer standard-library support from the language version alone.
