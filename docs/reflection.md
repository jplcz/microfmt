<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Experimental reflection support (P2996 / `-freflection`)

`jplcz_microfmt` depends on [`jplcz_reloco`](https://github.com/jplcz/reloco),
which detects [P2996 "Reflection for
C++26"](https://wg21.link/p2996) support (implemented experimentally by GCC
trunk 16+ behind `-freflection`) via the `RELOCO_HAS_REFLECTION` feature-test
macro. `microfmt` reuses that same macro -- `<microfmt/detail/compat.hpp>`
includes `<reloco/detail/compat.hpp>` directly rather than redefining its
own detection -- to provide one reflection-based formatter header:
[`microfmt/formatters/reflection.hpp`](formatters/ranges-and-structures.md#reflectionhpp).

See
[reloco's own reflection writeup](https://github.com/jplcz/reloco/blob/main/docs/reflection.md)
for the P2996 mechanics both libraries share: how `RELOCO_HAS_REFLECTION`
is detected, why `access_context::unchecked()` vs. `::current()` matters,
and the `^^T`/`typename [:expr:]`/`template for`/`define_static_array`
syntax. This document covers only what is specific to `microfmt`: how
`formatters/reflection.hpp` uses that same reflection support to format
enums and structs without Boost.

## Why this exists

`microfmt` already has a way to format described enums and structs:
[`boost_describe.hpp`](formatters/ranges-and-structures.md#boost_describehpp),
built on Boost.Describe/Boost.MP11. It works well, but it requires Boost as
a dependency and a `BOOST_DESCRIBE_STRUCT(Type, (), (field1, field2, ...))`
macro invocation per struct that must be kept in sync by hand whenever a
field is added, renamed, or removed.

Reflection removes both requirements. Given a complete class type,
`std::meta::nonstatic_data_members_of` enumerates every non-static data
member (name and type) directly from the compiler's own view of the class,
so there is no field list to write or maintain, and no Boost dependency to
add. `formatters/reflection.hpp` implements the identical output shape
(`{name: value, ...}` for structs, the declared name for enum values) using
only the standard library and the compiler's reflection support.

## Enums: automatic, no opt-in

Every enum type (scoped or unscoped) receives an automatic
`formatter<E>` once `formatters/reflection.hpp` is included -- no per-type
declaration needed, since no other builtin `microfmt` formatter targets
raw enum types (the integral formatter's `enable_if` explicitly excludes
them):

```cpp
enum class color { red, green, blue };

microfmt::format_to(out, "{}", color::green); // "green"
microfmt::format_to(out, "{}", static_cast<color>(99)); // "99" (fallback)
```

An unmapped/invalid enum value falls back to its underlying integer value,
identical to `boost_describe.hpp`'s behavior for the same case.

## Structs: opt-in via `MICROFMT_REFLECT_FORMAT`

A struct or class must opt in before it gets a reflection-based formatter:

```cpp
struct point { int x; int y; };
MICROFMT_REFLECT_FORMAT(point);

microfmt::format_to(out, "{}", point{3, 4}); // "{x: 3, y: 4}"
```

`MICROFMT_REFLECT_FORMAT(Type)` expands to a one-line
`microfmt::enable_reflect_format<Type>` specialization; `enable_reflect_
format` is the actual customization point, so a hand-written specialization
works identically to the macro.

This mirrors Rust's `#[derive(Debug)]`, which is likewise applied per type
rather than automatically available for every struct, for two reasons:

- **Ambiguity.** `microfmt::formatter<T, Enable>` dispatches through
  `enable_if`-based partial specializations (the same mechanism
  `boost_describe.hpp` and every other conditional formatter in `microfmt`
  use). A blanket "format every class type this way" formatter would
  produce an ambiguous partial specialization error the moment any other
  header's conditional formatter also matched the same `T` -- including
  `boost_describe.hpp`'s own struct formatter, if both headers were
  included for a `BOOST_DESCRIBE_STRUCT`-annotated type. Per-type opt-in
  avoids this entirely: only a type the caller explicitly named is ever a
  candidate.
- **Intent.** A plain aggregate struct is not necessarily meant to have a
  default debug-style rendering; requiring one line to opt in keeps that a
  deliberate choice, the same way leaving a type formatter-less today is a
  deliberate (if perhaps temporary) choice.

Only **public** non-static data members are enumerated (reflection walks
them via `std::meta::access_context::current()`, which respects normal
accessibility from the point of use, unlike `reloco`'s own trait
composition in `send_sync.hpp`/`relocatable.hpp`, which deliberately uses
`::unchecked()` -- see
[reloco's reflection writeup](https://github.com/jplcz/reloco/blob/main/docs/reflection.md)
for why those two use cases need different access contexts). A type with
private state that should still be formatted needs a hand-written
`formatter<T>` specialization instead, exactly as it would without this
header.

Nesting composes for free: a struct field that is itself a
`MICROFMT_REFLECT_FORMAT`-enabled struct, or an enum, formats recursively
through the ordinary `format_to(out, "{}", val.[:member:])` call each field
goes through -- see `examples/reflection_demo.cpp`'s `SystemState`, which
nests two reflected structs and two reflected enums three levels deep.

## Building and running the example

Requires a P2996-capable compiler (GCC 16+ trunk as of this writing):

```sh
g++-16 -std=c++26 -freflection -I include -I <path-to-reloco>/include \
    examples/reflection_demo.cpp -o reflection_demo
./reflection_demo
```

`examples/reflection_demo.cpp` declares the same `SensorReading`/
`NetworkConfig`/`SystemState`/`LinkStatus`/`LogLevel` types
`examples/describe_demo.cpp` does, so the two examples' output can be
compared side by side.

Including `microfmt/formatters/reflection.hpp` under any build that does
not pass `-freflection` (or does not select `-std=c++26`/`-std=gnu++26`) is
a hard `#error` at the top of the header, not a silent fallback -- the same
zero-risk-elsewhere guarantee `reloco`'s own reflection-gated code
provides, just enforced immediately for a standalone opt-in header instead
of an internal `#if` branch.

## Current limitations

- Not wired into `microfmt`'s CI or the `compile_all_headers.cpp` header
  smoke test, matching `reloco`'s own decision not to add reflection to
  its CI matrix: no mainstream, released compiler ships stable P2996
  support yet. Build and test it manually with GCC trunk in the meantime.
- No support (yet) for formatting a *union*'s members, or for including
  base-class members of a derived, reflection-formatted struct -- both are
  reachable with the same `std::meta` APIs `reloco` already uses for trait
  composition, but neither was needed to match `boost_describe.hpp`'s
  existing feature set, so neither is implemented.
