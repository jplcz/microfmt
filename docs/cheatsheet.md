<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Formatter cheatsheet

One lookup reference for every microfmt formatter: which header to include,
which types or factory functions it covers, every accepted `{:spec}` token,
and an example with its real output. Every example was compiled and run, and
examples that could not be built (for instance C++26 reflection) are marked
`(output unverified)`.

It is written for two readers. Humans can scan the tables when they forget a
flag; AI agents can load one page and copy the shown pattern. Each page uses
the same layout per formatter (**Include**, **Types / factories**, **Spec**,
**Example**, **Notes**) and ends with a quick index mapping "I want X" to the
view and spec.

For tutorials and the reasoning behind the design, use the
[formatter guide](formatters.md) and [Using microfmt](usage.md). This
cheatsheet is the compact, exhaustive reference.

## Pages

| Page | Covers |
|---|---|
| [Core and value formatters](cheatsheet/core.md) | Format-string syntax, integers, chars, bools, strings, pointers, `{:?}` and `as_debug`, output API and sinks, floats, strings, styled text, `std` interop types, fixed point, binary/base64, escaped text, hashes, errno/errors/exceptions, ANSI, `{fmt}` bridge |
| [Diagnostics, registers, and protocols](cheatsheet/diagnostics.md) | Raw pointers, registers, bitfields, hex dumps, MAC, semver, UUID, variants, register grids, CAN, I2C, SPI, JSON and CBOR writers |
| [Ranges, structures, Boost, and reloco](cheatsheet/structures.md) | `join`, `enumerate`, `zip`, `filter`, `repeat`, maps, tuples, vectors/matrices, optional/expected, reflection, Boost families, every `jplcz_reloco` type, `reloco::Debug` |
| [Time, units, inspector, markdown, and logging](cheatsheet/time-and-inspector.md) | `chrono`, calendar, bintime, POSIX time, units, source locations, remote-memory inspector views, Markdown writer, logger formatting |

## Rules that apply everywhere

These hold for all formatters, so the pages only mention exceptions.

* A replacement field is `{}`, `{:spec}`, `{N}`, or `{N:spec}`. `N` is a
  zero-based argument index. Literal braces are `{{` and `}}`. Named
  arguments and dynamic width or precision (`{:{}}`) are not supported.
* The spec belongs to that one argument. Most formatters scan their spec
  character by character, so token order is free and unknown characters are
  silently ignored. Check the **Spec** table of the formatter before relying
  on width, fill, or alignment: only integers, floats, strings, and
  `styled`/`as_string` views handle them, and each differs.
* Container-like views (ranges, tuples, maps, filters) consume an optional
  delimiter flag (`b` square, `c` curly, `n` none, `p` parentheses for
  tuples) and forward the rest of the spec to every element. Which flags a
  view accepts differs, so see its page.
* Views borrow their source. Keep the range, string, or byte buffer alive
  until formatting finishes.
* Include only the headers you use; a formatter does not exist until its
  header is included, and some headers rely on others (for example
  `math.hpp` needs `floating.hpp` for `float` elements).
* `{:?}` selects the debug form for types that support it (strings, chars);
  `microfmt::as_debug(lvalue)` forces `reloco::Debug<T>` priority.
* Output goes to a sink: `format_to(sink, ...)`, `format<N>(...)`,
  `print`/`println`. Application code should print through these rather than
  `printf`.

## Using this page as GitHub Pages source

The pages are plain Markdown under `docs/`, which the Doxygen configuration
already scans (`INPUT = include README.md docs`), so the
[publish-docs workflow](https://github.com/jplcz/microfmt/actions) renders
and deploys them together with the API reference. Build locally with
`scripts/build-docs.sh` and open `build/docs/html/index.html`.
