<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Inspector: GDB Remote Serial Protocol

The GDB protocol helpers build and parse Remote Serial Protocol (RSP) traffic
without heap allocation. They separate structured payload handling from
transport framing:

| Header | Purpose |
|---|---|
| `gdb_encoders.hpp` | Encodes client requests and server responses |
| `gdb_decoders.hpp` | Decodes request arguments and classifies responses |
| `gdb_stream.hpp` | Writes and incrementally validates `$payload#checksum` frames |
| `gdb_packet_recognizer.hpp` | Maps request prefixes to `packet_type` |
| `gdb_packet_metadata.hpp` | Provides packet names and protocol prefixes |

## Encode and decode requests

Use `client_request_encoder` when acting as a GDB frontend and
`client_request_decoder` when implementing a debug stub:

```cpp
microfmt::gdb::client_request_view request;
request.addr = 0x20000000;
request.length = 64;

microfmt::buffer_sink<64> payload;
microfmt::gdb::client_request_encoder::encode(
    payload.as_sink(), microfmt::gdb::packet_type::read_memory, request);
// m20000000,40

std::byte storage[128];
microfmt::scratch_allocator scratch(storage);
microfmt::gdb::client_request_view decoded;
auto type = microfmt::gdb::client_request_decoder::decode(
    payload.view(), scratch, decoded);
```

Decoded byte payloads borrow memory from `scratch_allocator`. Keep its backing
storage alive while using `client_request_view::data`, and reset or replace the
allocator before decoding the next packet when storage should be reused.
Scratch exhaustion produces an empty data span.

Binary `X` packets use RSP escaping for `#`, `$`, `}`, and `*`. Hexadecimal
`M`, `G`, and `P` packet data is converted to bytes in scratch storage.
`qSearch:memory`, thread selectors including `-1`, breakpoints, register
indices, and execution signals are exposed through the corresponding fields
of `client_request_view`. Unrecognized request prefixes return
`packet_type::unknown`.

The decoder expects a complete, unescaped, syntactically valid payload.
Transport framing and checksum validation belong to `gdb_streaming_decoder`.

## Encode and decode responses

`server_response_encoder` emits standard reply forms such as `OK`, `E16`,
hexadecimal data, thread lists, console output, and stop replies.
`server_response_decoder` classifies incoming replies and exposes status codes
or borrowed text:

```cpp
microfmt::gdb::server_response_view response;
response.status_code = 5;
response.thread_id = 0x2a;

microfmt::buffer_sink<64> payload;
microfmt::gdb::server_response_encoder::encode(
    payload.as_sink(), microfmt::gdb::server_response_type::stop_signal,
    response);
// T05thread:2a;
```

Use `server_response_decoder::decode_hex_data` with a scratch allocator to
convert memory, register, or console-output hexadecimal text into bytes.
Generic and protocol-specific text fields remain non-owning views into the
input payload.

## Frame payloads

`gdb_packet_writer` adapts a caller-owned character buffer to a
`microfmt::sink`. Write an encoded payload through `as_sink()`, then call
`finalize()` to append the checksum:

```cpp
char frame_storage[64];
microfmt::gdb::gdb_packet_writer frame(frame_storage);

microfmt::gdb::client_request_encoder::encode(
    frame.as_sink(), microfmt::gdb::packet_type::read_memory, request);
auto packet = frame.finalize();
```

An empty result from `finalize()` indicates insufficient frame storage.
`gdb_streaming_decoder` accepts bytes individually, ignores noise outside a
packet, unescapes binary payload bytes, and returns `ready` only after a valid
checksum. Its payload view borrows the decoder's scratch buffer and remains
valid until the decoder is reset or another packet is received.

Handle `error_overflow`, `error_checksum`, and `error_format` explicitly.
After any error, the decoder waits for the next `$` packet marker.

`examples/gdb_client_demo.cpp` provides a synchronous Boost.Asio client that
connects to a GDB server on `127.0.0.1:1234`, sends framed requests, handles
ACK/NAK traffic, validates response checksums, and decodes the response
payloads. The target is built only when Boost is available.
