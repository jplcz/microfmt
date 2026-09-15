<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: MIT
-->

# Protocols and structured output

The bus protocol views provide readable diagnostic output by default and
compact trace output on request. JSON and CBOR writers stream structured data
directly to a sink.

## Shared bus-format flags

CAN, I2C, and SPI views use the same leading flags:

| Flag | Effect |
|---|---|
| `c`, `C` | Compact trace representation |
| `x` | Lowercase hexadecimal digits |
| none | Verbose representation with uppercase hexadecimal |

All payload spans are borrowed and must remain valid during formatting.

## `can.hpp`

Use `can_frame(id, payload)` for an 11-bit CAN frame,
`can_extended(id, payload)` for a 29-bit frame, and `can_fd(...)` for CAN-FD.
`can_flags` and `can_frame_view` also represent remote frames and protocol
state explicitly.

```cpp
const uint8_t payload[]{0xde, 0xad, 0xbe, 0xef};
microfmt::format_to(out, MICROFMT_STRING("{:c}"),
                    microfmt::can_frame(0x123, payload));
```

See `examples/can_demo.cpp`.

## `i2c.hpp`

`i2c_write(address, payload)` and `i2c_read(address, payload)` construct
seven-bit transactions. `i2c_10bit(...)` represents a ten-bit transaction.
`i2c_status` reports success, address/data NACK, arbitration loss, or timeout.

```cpp
const uint8_t request[]{0x75};
microfmt::format_to(out, MICROFMT_STRING("{:c}"),
                    microfmt::i2c_write(0x68, request));
```

See `examples/i2c_demo.cpp`.

## `spi.hpp`

`spi_duplex(tx, rx, chip_select, mode)`, `spi_write(...)`, and
`spi_read(...)` construct SPI transfer views. Verbose output identifies MOSI,
MISO, chip select, mode, and transfer status.

```cpp
const uint8_t command[]{0x9f, 0x00, 0x00};
microfmt::format_to(out, MICROFMT_STRING("{:c}"),
                    microfmt::spi_write(command, 0));
```

See `examples/spi_demo.cpp`.

## `json.hpp`

`json::object_writer` and `json::array_writer` stream JSON directly to a sink.
Their RAII lifetime writes opening and closing delimiters, while `kv(...)` and
`val(...)` handle strings, booleans, null, and integral values.

`json::json_obj(lambda)` creates a formattable view:

```cpp
auto object = microfmt::json::json_obj([](auto &writer) {
  writer.kv("name", "microfmt").kv("ready", true).kv("count", 3);
});
microfmt::format_to(out, MICROFMT_STRING("{}"), object);
```

Strings are escaped as JSON strings. The view formatter has no custom
specifier. See `examples/json_demo.cpp`.

## `cbor.hpp`

`cbor::map_writer` and `cbor::array_writer` emit indefinite-length CBOR
containers directly to a sink. Construction writes the opening marker and
destruction or `end()` writes the break marker.

`cbor::cbor_map(lambda)` integrates map generation into a format call:

```cpp
auto map = microfmt::cbor::cbor_map([](auto &writer) {
  writer.kv("ready", true).kv("count", 3);
});
microfmt::format_to(out, MICROFMT_STRING("{}"), map);
```

The view formatter has no custom specifier. See `examples/cbor_demo.cpp`.
