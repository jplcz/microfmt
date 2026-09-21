<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Time, units, and source locations

These formatters handle standard clocks, operating-system time structures,
scaled engineering values, and source-code locations.

## `chrono.hpp`

`std::chrono::duration` values format with a suffix selected from their
period: `ns`, `us`, `ms`, `s`, `min`, `h`, or `d`. Unknown periods use a
custom-period marker. Use `c` or `C` to omit the suffix.

```cpp
microfmt::format_to(out, "{}", std::chrono::milliseconds{42});
// 42ms
```

`system_clock::time_point` supports:

| Flag | Output |
|---|---|
| `t`, `T` | Time only |
| `d`, `D` | Date only |
| none | Date and time |

`steady_clock::time_point` renders as an uptime-like duration. See
`examples/chrono_demo.cpp`.

## `bintime.hpp`

This header detects FreeBSD-style `bintime` structures with `sec` and `frac`
members and provides `as_sbintime(raw)` for integral `sbintime_t`-style
values.

| Flag | Fractional precision |
|---|---|
| `m`, `3` | Milliseconds |
| `u`, `6` | Microseconds |
| `n`, `9` | Nanoseconds |
| `p`, `1` | Picosecond mode |
| `r`, `R` | Omit the trailing `s` |

Specialize `is_bintime<T>` when a compatible platform type is not detected
automatically. See `examples/bintime_demo.cpp`.

## `posix_time.hpp`

Types exposing `tv_sec` and `tv_nsec` are treated as `timespec`-like values.
Types exposing `tv_sec` and `tv_usec` are treated as `timeval`-like values.

| Flag | Fractional precision |
|---|---|
| `m`, `3` | Three digits |
| `u`, `6` | Six digits |
| `n`, `9` | Nine digits; `timespec` only |
| `r`, `R` | Omit the trailing `s` |

The detection is structural and does not require a particular POSIX header.
See `examples/posix_time_demo.cpp`.

## `units.hpp`

`with_unit(value, suffix)` appends a fixed unit. `auto_si(value, suffix)` and
`auto_bytes(value)` select an SI or IEC prefix automatically. `hertz(value)`
is the integral frequency convenience wrapper.

```cpp
microfmt::format_to(out, "{:.2}", microfmt::hertz(50'000'000));
```

An optional single precision digit after `.` controls fractional output for
auto-scaled values. `auto_si`, `auto_bytes`, and `hertz` require integral
inputs. See `examples/units_demo.cpp`.

## `source_location.hpp`

`source_loc(...)` wraps a supported source-location object. With no argument,
it captures the current location when the platform API supports that form.

| Flag | Effect |
|---|---|
| `s`, `S` | Short `file:line` output |
| `f`, `F` | Include the function name |

Direct formatters are enabled for `std::source_location` in C++20 when
available. Defining `MICROFMT_ENABLE_BOOST_SOURCE_LOCATION` enables
`boost::source_location` support when its header is present. See
`examples/source_location_demo.cpp`.
