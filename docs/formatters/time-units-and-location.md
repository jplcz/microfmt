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

## `chrono_calendar.hpp`

C++20 calendar types have no formatter in `chrono.hpp` (which only covers
durations and clock time points). Include this header when you log dates
and times of day, for example from `std::chrono::year_month_day` parsed from
a config file. It compiles to nothing on standard libraries without the C++20
calendar (`MICROFMT_HAS_CHRONO_CALENDAR` is 0 there).

| Type | Output |
|---|---|
| `year`, `day` | zero-padded number (`2026`, `07`) |
| `month` | `03`, or `Mar` with `{:s}` |
| `weekday` | `Mon` (`?` when invalid) |
| `year_month_day` | `2026-03-07`; invalid dates append ` (invalid)` |
| `hh_mm_ss<D>` | `[-]HH:MM:SS[.fraction]`, fraction width follows `D` |

```cpp
#include <microfmt/formatters/chrono_calendar.hpp>
using namespace std::chrono;

// date -> "2026-03-07"; weekday -> "Mon"; month with {:s} -> "Mar"
microfmt::format_to(out, "{} {} {:s}", year_month_day{year{2026}/March/7},
                    weekday{1}, month{3});
// 1 h 2 s 250 ms before midnight offset -> "-01:00:02.250"
microfmt::format_to(out, "{}", hh_mm_ss{-(1h + 2s + 250ms)});
```

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
| `f`, `F` | Include the function name (already the default; only `s` hides it) |

Direct formatters are enabled for `std::source_location` in C++20 when
available. Defining `MICROFMT_ENABLE_BOOST_SOURCE_LOCATION` enables
`boost::source_location` support when its header is present. See
`examples/source_location_demo.cpp`.
