<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Shared-library deployments and code size

`jplcz_microfmt` is header-only by default: every translation unit (and,
critically, every shared object) that `#include`s a microfmt header gets
its own copy of every `inline`/template entity it actually instantiates
(`vformat_to`, `formatter<T>::format`, the type-erased
`format_type_thunk<T>` trampoline, ...). For a single executable or a
single `.so`, the linker already deduplicates that down to one copy of
each. In a **multi-`.so` deployment** -- one main application plus several
independently-built plugin/feature `.so`'s that each `#include` microfmt --
that deduplication only happens *within* one `.so`'s link step, never
*across* separate `-shared` invocations, so every shared object pays for
its own copy of the same code. `tools/codesize/testbed/` measures exactly
this: see its README for numbers from a realistic synthetic application
(typically ~20% of total system code size, in the configurations
measured there).

This guide covers the two mechanisms microfmt provides to fix that:

1. **`MICROFMT_SHARED` / `MICROFMT_SHARED_BUILD`** -- for microfmt's own
   built-in, concrete formatters (things microfmt itself defines, like
   `formatter<std::error_code>` or `format_hexdump`).
2. **`MICROFMT_FORMATTER_INSTANCE`** -- for formatters that are themselves
   templated over a type *your* application supplies (like
   `formatter<std::optional<int>>` or `formatter<reloco::vector<T>>`),
   which microfmt cannot pre-migrate since it doesn't know your types
   ahead of time.

Both are entirely opt-in: a plain header-only build (the default) is
completely unaffected by either.

## Background: why `extern template` isn't automatic here

The standard C++ way to deduplicate a template instantiation across
translation units is `extern template` (declare it in every consumer,
define it in exactly one place). microfmt uses exactly this mechanism for
`MICROFMT_FORMATTER_INSTANCE` below -- but it does **not** work for every
microfmt internal, because some of them (e.g. the integer-formatting core)
are marked force-inline for performance and end up inlined directly into
every call site with no separate symbol to "extern" against; declaring
them `extern template` would silently do nothing. Where that applies,
microfmt instead exposes a small set of genuine non-template
`MICROFMT_API` overloads for the concrete types it actually needs (see
`microfmt/detail/compat.hpp`) -- this is transparent to you as a caller and
requires no action on your part beyond enabling `MICROFMT_SHARED`.

## Part 1: `MICROFMT_SHARED` for microfmt's own concrete formatters

Define `MICROFMT_SHARED` (to any value) consistently in **every**
translation unit in your program, across every `.so` and the main
executable -- consistently, because it changes the linkage of affected
declarations (`inline` vs. plain declaration vs. exported), and mixing
the two in one program is an ODR violation.

Exactly one translation unit -- the one building your actual shared
library of microfmt internals -- must *additionally* define
`MICROFMT_SHARED_BUILD`, and should simply include the umbrella header
instead of hand-picking formatter headers:

```cpp
// microfmt_shared_lib.cpp -- the one .cpp file, built `-shared`
#define MICROFMT_SHARED
#define MICROFMT_SHARED_BUILD
#include <microfmt/microfmt_compile.hpp>
```

Build it as a real shared library (e.g. `libmicrofmt_shared.so`) and link
every other `.so`/executable in your deployment against it. Every other
translation unit only needs `MICROFMT_SHARED` defined and includes
whichever individual `formatters/*.hpp` headers it actually uses, exactly
as in a normal header-only build:

```cpp
// any consumer .cpp, in any shard .so or the main executable
#define MICROFMT_SHARED
#include <microfmt/microfmt.hpp>
#include <microfmt/formatters/error.hpp>
#include <microfmt/formatters/semver.hpp>
// ...
```

CMake sketch, using the `jplcz_microfmt::microfmt-shared`/`jplcz_microfmt::
microfmt-shared-export` INTERFACE targets microfmt's own `CMakeLists.txt`
provides (they only forward the `MICROFMT_SHARED`/`MICROFMT_SHARED_BUILD`
compile definitions above -- microfmt is header-only and does not build or
export an actual shared object for you; `microfmt_shared` below is *your*
`SHARED` library target, built from your own `MICROFMT_SHARED_BUILD`
translation unit):

```cmake
add_library(microfmt_shared SHARED microfmt_shared_lib.cpp)
target_link_libraries(microfmt_shared PUBLIC jplcz_microfmt::microfmt-shared-export)

add_library(my_plugin SHARED my_plugin.cpp)
target_link_libraries(my_plugin PRIVATE microfmt_shared jplcz_microfmt::microfmt-shared)
```

`jplcz_microfmt::microfmt-shared-export` forwards `jplcz_reloco::
reloco-shared` (plain `RELOCO_SHARED`, an ordinary consumer) alongside
`MICROFMT_SHARED_BUILD` -- **never** `jplcz_reloco::reloco-shared-export`
(`RELOCO_SHARED_BUILD`). microfmt embeds reloco as an ordinary header
dependency and never builds reloco's own shared library itself, so your
`microfmt_shared` translation unit must remain an ordinary `RELOCO_SHARED`
*consumer* of reloco, exactly like every other `MICROFMT_SHARED` consumer.
Linking `reloco-shared-export` here instead would wrongly make this
translation unit emit reloco's own non-template backend definitions
(`mutex.hpp`/`heap_allocator.hpp`/... bodies) as if it were the one
canonical translation unit building reloco's own shared library, which it
is not -- see [reloco's own shared-library guide](https://github.com/jplcz/reloco/blob/main/docs/shared-library.md)
if you additionally want to build and link against an actual
`libreloco_shared.so`.


`microfmt_compile.hpp`'s doc comment lists every entity currently migrated
this way (as of this writing: `vformat_to`,
`int_formatter_specs::format_int_impl`, and the concrete formatters for
hexdump, `std::error_code`/`std::error_category`, `semver`,
`styled_str_view`, `raw_ptr_view`, `escaped_view`, `bitfield_view`, and
uuid byte formatting) -- growing over time as more of microfmt's own
formatters gain the same treatment. You don't need to track that list
yourself; the umbrella header does.

This mechanism only ever covers formatters for types **microfmt itself
defines**. It cannot help with `formatter<std::optional<int>>`,
`formatter<reloco::vector<std::string>>`, or any other specialization that
is templated over a type your application supplies -- for those, see
Part 2.

## Part 2: `MICROFMT_FORMATTER_INSTANCE` for your own formatter instantiations

Most real formatting call sites don't just use microfmt's own concrete
types -- they format `std::optional<int>`, `std::vector<MyType>`,
`reloco::flat_map<K, V>`, and similar containers/wrappers over
application-defined element types. microfmt cannot pre-migrate those
(it doesn't know `MyType` ahead of time), but **you** can, using
`<microfmt/microfmt_extern.hpp>`'s `MICROFMT_FORMATTER_INSTANCE(Type)`
macro -- a thin, opt-in wrapper around ordinary C++ explicit template
instantiation.

### How it works

`MICROFMT_FORMATTER_INSTANCE(Type)` expands differently depending on which
of `MICROFMT_SHARED`/`MICROFMT_SHARED_BUILD` are defined in the
translation unit that uses it:

| Translation unit defines...            | Expands to                                              |
|-----------------------------------------|----------------------------------------------------------|
| neither (plain header-only build)       | nothing (a no-op)                                        |
| `MICROFMT_SHARED` only (a consumer)     | `extern template` **declarations** for that `Type`        |
| `MICROFMT_SHARED_BUILD` (the build TU)  | explicit instantiation **definitions** for that `Type`    |

This is exactly the same macro invocation in both places -- what changes
is only which macros are defined in that particular `.cpp` file, so you
write the list of types **once**, in one shared header, and include it
from both sides.

### Step-by-step

1. **Pick your shared "instance list" header.** This is application code,
   not microfmt code -- create e.g. `myapp_formatter_instances.hpp`:

   ```cpp
   // myapp_formatter_instances.hpp
   #pragma once
   #include <microfmt/microfmt_extern.hpp>
   #include <microfmt/formatters/monad.hpp>   // formatter<std::optional<T>>
   #include <microfmt/formatters/reloco.hpp>  // formatter<reloco::flat_map<K,V>>
   #include <optional>

   // List every (formatter<Type>, format_type_thunk<Type>) pair your
   // application actually formats and wants deduplicated across shared
   // objects. Types with commas (e.g. reloco::flat_map<int, int>) work
   // fine -- the macro is variadic.
   MICROFMT_FORMATTER_INSTANCE(std::optional<int>);
   MICROFMT_FORMATTER_INSTANCE(std::optional<std::string>);
   MICROFMT_FORMATTER_INSTANCE(reloco::flat_map<int, int>);
   ```

2. **Include it from your `MICROFMT_SHARED_BUILD` translation unit**
   (typically right after `microfmt_compile.hpp`):

   ```cpp
   // microfmt_shared_lib.cpp
   #define MICROFMT_SHARED
   #define MICROFMT_SHARED_BUILD
   #include <microfmt/microfmt_compile.hpp>
   #include "myapp_formatter_instances.hpp"
   ```

   This is what actually generates the `formatter<Type>`/
   `format_type_thunk<Type>` bodies, exported once from
   `libmicrofmt_shared.so`.

3. **Include the same header from every consumer translation unit** that
   formats one of those types:

   ```cpp
   // my_plugin.cpp
   #define MICROFMT_SHARED
   #include <microfmt/microfmt.hpp>
   #include "myapp_formatter_instances.hpp"

   void log_status(const microfmt::sink &out, std::optional<int> code) {
     microfmt::format_to(out, "status={}\n", code);
   }
   ```

   The `extern template` declarations from step 1 suppress this
   translation unit's own local instantiation of `formatter<
   std::optional<int>>`/`format_type_thunk<std::optional<int>>` and bind
   the call to `libmicrofmt_shared.so`'s copy instead.

4. **Link every consumer against the shared library** built in step 2, as
   in Part 1's CMake sketch.

### Keeping the two sides in sync

Because the instance list is application-authored (not generated), a
mismatch is possible if you edit it carelessly -- e.g. a type used with
`microfmt::format_to` somewhere but never listed, or listed in the build
TU but a consumer including a different, out-of-date copy of the header.
Both failure modes surface as an ordinary linker "undefined reference"
error at link time, not a silent miscompile or runtime bug -- exactly the
same failure mode as forgetting to link against the library at all. Using
one single, shared header for both sides (as shown above) is the simplest
way to avoid this: there is only one list to keep correct.

### What NOT to do

Don't try to `extern template` microfmt's own force-inlined internals
(e.g. anything in `int_formatter_specs`) yourself -- as explained above,
`extern template` silently does nothing for those; they're already
handled by `MICROFMT_SHARED` alone via non-template overloads. Only apply
`MICROFMT_FORMATTER_INSTANCE` to `formatter<Type>` specializations that
are ordinary (non-force-inlined) templates over your own types --
`formatter<std::optional<T>>`, `formatter<std::vector<T>>`,
`formatter<reloco::vector<T>>`, and similar container/wrapper formatters
all qualify.

## General guidance for reducing code size in multi-`.so` projects

Beyond the two mechanisms above, a few broader points from
`tools/codesize/testbed/README.md`'s findings are worth repeating here:

- **`-fvisibility=hidden`/`-fvisibility-inlines-hidden` do not fix
  cross-`.so` duplication.** They shrink `.dynsym`/`.dynstr`/relocation
  overhead (sometimes substantially), but the compiler still emits a full
  copy of every weak/inline symbol's *body* into each shared object either
  way -- `.text`/`.rodata` are unaffected, or can even grow slightly.
  Measure with `tools/codesize/testbed/measure.py multiso` (or your own
  equivalent) before trusting a size win attributed to visibility flags
  alone.
- **Reduce your argument-type catalog, not just plumbing.** Every distinct
  `Args...` combination passed to `microfmt::format_to` is its own
  template instantiation. Normalizing scattered ad hoc `format_to<Args...>`
  call sites onto a smaller, shared set of logging/formatting wrapper
  functions (or fewer distinct container/wrapper element types) shrinks
  both the per-`.so` cost and the duplication `MICROFMT_FORMATTER_INSTANCE`
  would otherwise need to cover.
- **Consider merging plugins that are always loaded together.** If two
  `.so`'s are never deployed independently, merging them into one removes
  the duplication at the source instead of deduplicating after the fact.
  `measure.py multiso`'s "system total" makes the `N=1` case directly
  comparable to `N>1`, so you can quantify this trade-off for your own
  deployment shape before deciding.
- **Measure your own application, not just the testbed's synthetic one.**
  `tools/codesize/testbed/` is a stand-in for a *typical* multi-file,
  multi-`.so` application; its numbers are illustrative, not a guarantee
  for your specific type/call-site catalog. Use `measure.py topsymbols`/
  `measure.py multiso` against your own binaries to find your actual
  biggest offenders before spending migration effort.

## See also

- [`microfmt/detail/compat.hpp`](../include/microfmt/detail/compat.hpp) --
  the full `MICROFMT_API`/`MICROFMT_API_CONSTEXPR`/
  `MICROFMT_SHARED_PROVIDE_DEFINITIONS` macro contract underlying Part 1.
- [`microfmt/microfmt_compile.hpp`](../include/microfmt/microfmt_compile.hpp)
  -- the umbrella header for the `MICROFMT_SHARED_BUILD` translation unit.
- [`microfmt/microfmt_extern.hpp`](../include/microfmt/microfmt_extern.hpp)
  -- `MICROFMT_FORMATTER_INSTANCE`'s full doc comment and implementation.
- [`CMakeLists.txt`](../CMakeLists.txt) -- the `jplcz_microfmt::
  microfmt-shared`/`jplcz_microfmt::microfmt-shared-export` INTERFACE
  targets (`MICROFMT_SHARED`/`MICROFMT_SHARED_BUILD` forwarders,
  respectively -- the latter also forwarding `jplcz_reloco::reloco-shared`,
  never `reloco-shared-export`) used in Part 1's CMake sketch above.
- [`tools/codesize/testbed/README.md`](../tools/codesize/testbed/README.md)
  -- the measurement testbed this guide's numbers and structural-fix
  suggestions are drawn from, including how to run it against your own
  compiler flags.
