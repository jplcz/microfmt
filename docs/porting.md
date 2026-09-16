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

## Adding a platform or compiler

Keep compiler-specific syntax inside `compat.hpp`. Prefer standard feature-test
macros after conditionally including the corresponding standard header, and
provide a conservative `0` or empty fallback when a feature is unavailable.
Do not infer standard-library support from the language version alone.
