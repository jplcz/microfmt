<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Values, binary, and diagnostics

These formatters adapt scalar values, byte sequences, addresses, and
diagnostic metadata into bounded textual views.

## `base_views.hpp`

`base64(span<const uint8_t>)` and the fixed-array overload return a
`base64_view`. The formatter emits RFC-style base64 text directly to the sink
and accepts no custom specifier.

```cpp
const uint8_t payload[]{'M', 'a', 'n'};
microfmt::format_to(out, "{}", microfmt::base64(payload));
// TWFu
```

The view borrows the byte sequence. See `examples/base_views_demo.cpp`.

## `binary.hpp`

`bin(value)`, `bin<Bits>(value)`, `bin_prefixed(value)`, and
`bin_grouped(value)` construct `binary_view` values for integral types other
than `bool`.

| Flag | Effect |
|---|---|
| `#` | Prefix output with `0b` |
| `_` | Group digits into four-bit nibbles |

```cpp
microfmt::format_to(out, "{:#_}", microfmt::bin<16>(uint16_t{0xa53c}));
```

See `examples/binary_demo.cpp`.

## `bitfield.hpp`

`bitfield_view` renders a `uint32_t` using a table of `bit_field`
descriptors. Build views with `bits(value, fields)`. Use
`MICROFMT_BIT_FLAG`, `MICROFMT_BIT_VALUE_DEC`, and
`MICROFMT_BIT_VALUE_HEX` to define descriptors, or
`MICROFMT_DEFINE_REGISTER_TYPE` to define a reusable register wrapper.

Fields with a zero mask are skipped. The formatter has no custom specifier.

```cpp
constexpr microfmt::bit_field fields[]{
    MICROFMT_BIT_FLAG("enabled", 0),
    MICROFMT_BIT_VALUE_HEX("mode", 0x0e, 1),
};
microfmt::format_to(out, "{}", microfmt::bits(status, fields));
```

See `examples/bitfield_demo.cpp`.

## `escaped.hpp`

`escaped(...)` creates an `escaped_view` over a string, character buffer,
byte span, or fixed array. It emits C-style escapes for control and
non-printable bytes. View construction controls quoting and whether quote
characters are escaped; there are no format-string flags.

```cpp
microfmt::format_to(out, "{}", microfmt::escaped("line 1\nline 2"));
```

See `examples/escaped_demo.cpp`.

## `fixed_point.hpp`

`fixed<Scale, Decimals>(value)` formats an integer as a fixed-point number.
Convenience aliases and factories are provided for milli-, centi-, and
micro-scaled values: `milli`, `centi`, and `micro`.

`Scale` must be positive, `Decimals` must not exceed 18, and the stored value
must be integral. The formatter has no custom specifier.

```cpp
microfmt::format_to(out, "{} V", microfmt::milli(3300));
// 3.300 V
```

See `examples/fixedpoint_demo.cpp`.

## `floating.hpp`

This header adds direct formatters for `float`, `double`, and `long double`.
Its syntax follows the supported `printf` floating-point subset:

| Syntax | Effect |
|---|---|
| `+` or space | Select sign handling |
| `#` | Request the alternate form |
| `.N` | Set decimal precision |
| `f`, `F` | Fixed notation |
| `e`, `E` | Scientific notation |
| `g`, `G` | Shortest fixed/scientific form |
| `a`, `A` | Hexadecimal floating-point form |

```cpp
microfmt::format_to(out, "{:+.3f}", voltage);
```

Formatting uses `snprintf`. It starts with bounded local storage but may use a
heap fallback when the generated representation exceeds that capacity, so do
not use this formatter where allocation is forbidden.

## `format_helpers.hpp`

This header collects compact diagnostic adapters:

| Factory | Output |
|---|---|
| `hex(value)` | Hexadecimal integer |
| `bin(value)` | Binary integer |
| `bytes(value)` | Human-readable byte count |
| `addr_offset(address, base)` | Address plus relative offset |
| `mem_range(begin, end)` | Memory interval |
| `align(text, width, mode, fill)` | Left, right, or centered text |
| `join(span, delimiter)` | Delimited span elements |

These helper formatters accept no additional format-string flags; configure
them through their factory arguments.

## `hash.hpp`

`as_hash(value)` returns a `hash_view` that computes and formats
`std::hash<T>{}(value)`. It participates only when `std::hash<T>` is callable.
The full format specifier is forwarded to the resulting `size_t` value.

```cpp
microfmt::format_to(out, "{:016X}", microfmt::as_hash(key));
```

See `examples/hash_demo.cpp`.

## `hexdump.hpp`

`hexdump(data, base_address)` creates a direct byte-span dump.
`hexdump_checked(...)` reads through a `memory_reader_fn_t` callback for
fault-aware target memory. Both produce `hexdump_view`.

```cpp
uint8_t line_storage[16];
auto view = microfmt::hexdump_checked(address, length, reader, reader_context);
microfmt::format_hexdump(view, line_storage, out);
```

`format_hexdump` accepts caller-owned line storage and caps the effective line
width at 32 bytes. The ordinary `formatter<hexdump_view>` and `hexdump_to`
convenience function use a bounded 32-byte local line buffer. Use
`format_hexdump` when the buffer must live outside the stack.

Hex dumps can include addresses and an ASCII pane. A zero line width defaults
to 16 bytes. See `examples/hexdump_demo.cpp`.

## `net.hpp`

`mac(...)` creates a `mac_view` for six-byte MAC/EUI-48 or eight-byte EUI-64
addresses. Inputs with another size render as a zero MAC address.

| Flag | Effect |
|---|---|
| `x` | Lowercase hexadecimal |
| `X` | Uppercase hexadecimal |
| `:`, `-`, `.`, `_` | Select byte separator |

```cpp
microfmt::format_to(out, "{:X-}", microfmt::mac(address_bytes));
```

## `pointer.hpp`

`raw_ptr(pointer)` and `raw_ptr32(pointer)` provide deterministic address
formatting. `raw_range(first, last)` formats a contiguous pointer range.

Pointer flags select prefix, letter case, width, and null handling:

| Flag | Effect |
|---|---|
| `p`, `P` | Include the `0x` prefix |
| `x`, `X` | Hexadecimal without a prefix |
| `32`, `64` | Force compatibility-width output |
| `0N` | Set zero-padded width |
| `z` | Render null as `0x0` instead of `(nil)` |

For ranges, `b`, `c`, and `n` select square, curly, or no delimiters; the
remaining specifier is forwarded to each element. See
`examples/pointer_demo.cpp`.

## `register.hpp`

`reg_descriptor<N>` stores a register name and fixed array of `reg_field`
descriptors. `format_reg(value, descriptor)` creates a `reg_view` that prints
the raw register value and decoded fields.

| Flag | Effect |
|---|---|
| `s`, `S` | Short output |
| `n`, `N` | Omit outer brackets |

The descriptor is normally `constexpr` so field names and masks remain in
read-only storage. See `examples/register_demo.cpp`.

## `semver.hpp`

`semver` stores major, minor, and patch values plus optional prerelease and
build metadata. Construct it with `version(...)`, `from_packed32(...)`, or
`from_packed24(...)`.

| Flag | Effect |
|---|---|
| `#`, `v`, `V` | Add a leading `v` |
| `c`, `C` | Compact output without prerelease/build metadata |

The metadata fields are borrowed `microfmt::string_view` values. See
`examples/semver_demo.cpp`.

## `string.hpp`

This header formats `std::basic_string` directly and provides `as_string(...)`
for advanced formatting of string views, character arrays, and pointers.

The advanced syntax is `[[fill]align][width][.precision][?]`:

| Syntax | Effect |
|---|---|
| `<`, `>`, `^` | Left, right, or center alignment |
| `width` | Minimum field width |
| `.precision` | Maximum source length |
| `?` | Debug quoting and escaping |

```cpp
microfmt::format_to(out, "{:*>12.8?}", microfmt::as_string(name));
```

A null C string renders as `(null)`.

## `uuid.hpp`

`uuid(...)` creates a `uuid_view` from a 16-byte span, fixed array, or
compatible contiguous container.

| Flag | Effect |
|---|---|
| `X` | Uppercase hexadecimal |
| `#` | Surround the UUID with braces |

When `MICROFMT_ENABLE_BOOST_UUID` is enabled and Boost.UUID is available, the
header also formats `boost::uuids::uuid` directly. Short input spans render as
the canonical zero UUID. See `examples/uuid_demo.cpp`.

## `variant.hpp`

`std::variant` and `std::monostate` receive direct formatters.
`as_variant(value)` creates a configurable non-owning `variant_view`;
`as_debug_variant(value)` enables the descriptive wrapper.

| Leading flag | Effect |
|---|---|
| `?`, `i`, `#` | Include variant/index diagnostic information |

The remaining specifier is forwarded to the active alternative. A
`valueless_by_exception()` variant renders as `valueless`. See
`examples/variant_demo.cpp`.
