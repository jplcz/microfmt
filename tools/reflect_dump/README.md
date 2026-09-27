<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# `reflect_dump`: legacy-compiler codegen for `formatters/reflection.hpp`

Runs P2996 reflection once, offline, on a `-freflection` toolchain, and
prints an equivalent, plain-C++ `formatter<T>`/`formatter<E>` header --
literal `switch`/`out.write(...)` code, no reflection syntax at all -- for
any ordinary compiler to consume instead. See
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

3. Run it, redirecting stdout to a header your legacy build includes:

   ```sh
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

## Regenerating after a type changes

Re-run steps 2-3 above whenever an annotated type's fields change. The
generated header has no way to detect staleness itself -- it is a frozen
snapshot -- so this tool is intended for a build step or a manual "make
generate" -style workflow, not something invoked automatically inside the
main library's own build.
