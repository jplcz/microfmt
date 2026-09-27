<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# `reflect_dump`: legacy-compiler codegen for `formatters/reflection.hpp`

Runs P2996 reflection once, offline, on a `-freflection` toolchain, and
prints plain C++ header -- literal `switch`/`out.write(...)` code, no
reflection syntax at all -- for any ordinary compiler to consume instead:
an enum gets a `formatter<E>` specialization (same as
`formatters/reflection.hpp`'s live formatter); a struct gets a
`reloco::Debug<T>` specialization instead, rendering Rust
`#[derive(Debug)]`-style `TypeName { field: value, ... }` text. See
[`docs/reflection.md`](../../docs/reflection.md) for the full writeup.

## Quick start

1. Annotate your own types with `MICROFMT_REFLECT_FORMAT`/
   `MICROFMT_REFLECT_DUMP_ENUM` from `<microfmt/formatters/reflect_annotate.hpp>`
   (always safe to include; see that header's own docs). `example_manifest.hpp`
   in this directory shows the expected shape.
2. Build this tool with a P2996-capable compiler (GCC 16+ trunk as of this
   writing), pointing `MICROFMT_REFLECT_DUMP_MANIFEST` at your own header:

   ```sh
   g++-16 -std=c++26 -freflection -DMICROFMT_REFLECT_DUMP_MODE \
       -DMICROFMT_REFLECT_DUMP_MANIFEST='"your_types.hpp"' \
       -I include -I <path-to-reloco>/include \
       tools/reflect_dump/reflect_dump_main.cpp -o reflect_dump
   ```

3. Run it, writing the generated header either by passing an output path
   as the sole argument, or by redirecting stdout (both are equivalent;
   the CMake integration below uses the argument form so it never depends
   on shell redirection support):

   ```sh
   ./reflect_dump generated/reflect_formatters.hpp
   # or:
   ./reflect_dump > generated/reflect_formatters.hpp
   ```

4. In application code built by a compiler without `-freflection`, include
   your type header (its `MICROFMT_REFLECT_FORMAT`/`MICROFMT_REFLECT_DUMP_ENUM`
   calls are a no-op there) followed by the generated header:

   ```cpp
   #include "your_types.hpp"
   #include "generated/reflect_formatters.hpp"
   #include <microfmt/microfmt.hpp>

   microfmt::format_to(out, "{}", your_value); // works, no reflection needed
   ```

The generated header is self-guarded with `#if !RELOCO_HAS_REFLECTION`, so
it is also safe to include unconditionally even in a build that *does*
have reflection and separately includes the live
`microfmt/formatters/reflection.hpp` -- the generated, possibly-stale
specializations are skipped there in favor of the always-correct live
ones.

## CMake integration

`cmake/ReflectDump.cmake` (in the repository root, alongside this
library's own top-level `CMakeLists.txt`) provides a reusable
`jplcz_microfmt_add_reflect_dump(<target> COMPILER ... MANIFEST ...
OUTPUT ... INCLUDE_DIRS ...)` function that wraps steps 2-3 above into a
regular custom build target (`cmake --build . --target <target>`
compiles the generator and (re-)writes `OUTPUT`). It is deliberately
*not* wired into `jplcz_microfmt`'s own `CMakeLists.txt`/build -- see
[`tools/reflect_dump/CMakeLists.txt`](CMakeLists.txt) for a standalone
demo project that uses it, buildable independently of the main library:

```sh
cmake -S tools/reflect_dump -B build-reflect-dump \
    -DJPLCZ_MICROFMT_REFLECT_DUMP_COMPILER=g++-16 \
    -DJPLCZ_MICROFMT_REFLECT_DUMP_RELOCO_INCLUDE_DIR=/path/to/reloco/include
cmake --build build-reflect-dump
./build-reflect-dump/reflect_dump_demo
```

Or, one click:

```sh
./run_demo.sh g++-16 /path/to/reloco/include
```

`run_demo.sh` configures, builds, and runs the demo above in one step; any
trailing arguments are forwarded to `cmake` (e.g. to also pass
`-DCMAKE_TOOLCHAIN_FILE=...` -- see the cross-compilation note below and
the caveat at the top of the script about running a cross-built binary
yourself instead of relying on the script's final "run" step).

Two properties matter for real (potentially cross-compiling) projects
that adopt the function:

* **`COMPILER` is always explicit, never auto-detected or PATH-searched
  for.** A guessed "first `g++` found" could silently be the wrong
  compiler -- wrong version, or, under some cross toolchain files, the
  cross-compiler itself -- so the caller must name a real, already
  verified P2996-capable (`-freflection`) compiler by hand.
* **The generator always builds and runs on/for the host, completely
  independent of `CMAKE_CXX_COMPILER`/`CMAKE_CROSSCOMPILING`/any
  toolchain file.** `COMPILER` is invoked directly by its own absolute
  command line rather than through CMake's normal compiler abstraction,
  so this works correctly even when the project's own `CXX` toolchain
  is a cross-compiler that cannot build (or run) a `-freflection`
  binary at all -- confirmed by cross-building the demo above for ARM
  (`-DCMAKE_TOOLCHAIN_FILE=.../toolchain-arm-linux-gnueabi.cmake`): the
  generator still ran as a native host binary during the build, while
  `reflect_dump_demo` itself came out as an ordinary ARM binary
  consuming the generated header, with no reflection involved at all.



Re-run steps 2-3 above whenever an annotated type's fields change. The
generated header has no way to detect staleness itself -- it is a frozen
snapshot -- so this tool is intended for a build step or a manual "make
generate" -style workflow, not something invoked automatically inside the
main library's own build.
