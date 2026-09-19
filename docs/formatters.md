<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Formatter guide

The formatter library extends the core `microfmt::formatter<T>` customization
point with small values and non-owning views for diagnostics, protocols,
containers, time values, and presentation. Include only the formatter headers
used by an application.

Most formatter factories return lightweight views. The source values,
containers, byte spans, and strings referenced by those views must remain
alive until formatting finishes. Formatting writes directly to a
`microfmt::sink`; formatters do not first build an intermediate output string.

## Start here

A typical formatter call has three parts:

```cpp
#include <microfmt/formatters/binary.hpp>
#include <microfmt/microfmt.hpp>

microfmt::format_to(output, MICROFMT_STRING("status={:#_}"),
                    microfmt::bin(status));
```

1. Include the header that defines the formatter or view.
2. Construct a view when the source type does not have a direct formatter.
3. Place formatter-specific flags after `:` in the replacement field.

Use `MICROFMT_STRING(...)` for literal format strings so parsing and argument
dispatch happen at compile time. Reserve it for hot paths; each distinct
format string and argument-type combination generates its own unrolled
code, so using it indiscriminately grows code size. See [Using
microfmt](usage.md) for core format-string syntax and [Writing low-stack
renderers](renderer-guide.md) when implementing application-specific
formatters.

## Subsystem guides

| Guide | Use it for | Formatter headers |
|---|---|---|
| [Values, binary, and diagnostics](formatters/values-and-binary.md) | Binary/base64 data, escaped strings, numeric wrappers, pointers, hashes, bitfields, registers, dumps, versions, UUIDs, and variants | `base_views.hpp`, `binary.hpp`, `bitfield.hpp`, `escaped.hpp`, `fixed_point.hpp`, `floating.hpp`, `format_helpers.hpp`, `hash.hpp`, `hexdump.hpp`, `net.hpp`, `pointer.hpp`, `register.hpp`, `semver.hpp`, `string.hpp`, `uuid.hpp`, `variant.hpp` |
| [Ranges and structured values](formatters/ranges-and-structures.md) | Joined, filtered, and repeated ranges, tuples, maps, grids, vectors, matrices, monads, reflected types, and Boost integrations | `filter_view.hpp`, `grid_view.hpp`, `map_view.hpp`, `math.hpp`, `monad.hpp`, `ranges.hpp`, `repeated_view.hpp`, `tuple.hpp`, `boost_describe.hpp`, `boost_containers.hpp`, `boost_monad.hpp`, `boost_net.hpp`, `boost_system.hpp`, `boost_time.hpp`, `boost_values.hpp` |
| [Time, units, and source locations](formatters/time-units-and-location.md) | Standard and POSIX clocks, binary time, scaled units, and call-site information | `bintime.hpp`, `chrono.hpp`, `posix_time.hpp`, `units.hpp`, `source_location.hpp` |
| [Protocols and structured output](formatters/protocols-and-data.md) | CAN, I2C, SPI, JSON, and CBOR diagnostics or streaming output | `can.hpp`, `i2c.hpp`, `spi.hpp`, `json.hpp`, `cbor.hpp` |
| [Presentation and compatibility](formatters/presentation-and-bridges.md) | ANSI styling, text layout, and the `{fmt}` compatibility bridge | `ansi.hpp`, `styled.hpp`, `fmt.hpp` |

## Common formatter rules

* A formatter-specific specifier applies only to the argument in that
  replacement field.
* Views do not own their source ranges, strings, descriptors, or byte buffers.
* Range-like formatters commonly use `b`, `c`, or `n` to select square,
  curly, or no outer delimiters, then forward the remaining specifier to each
  element.
* Protocol views use `c` for compact trace output and `x` for lowercase
  hexadecimal output.
* Optional standard-library and Boost formatters are exposed only when their
  feature macros and headers are available.
* Prefer caller-owned scratch storage for fault-checked dumps or custom
  renderers used on constrained stacks.

The `examples/` directory contains runnable demonstrations for nearly every
formatter family. Each formatter section links to its closest example.
