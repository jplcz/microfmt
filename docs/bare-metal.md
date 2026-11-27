<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Bare-metal hardware sinks

microfmt ships a small set of zero-allocation `microfmt::sink` adapters for
common bare-metal targets under `microfmt/hw/`. Each exposes an `as_sink()`
method returning a type-erased `microfmt::sink` usable directly with
`format_to`. See `examples/bare_metal/qemu-virt/` for a complete AArch64
freestanding kernel that combines both sinks.

## ARM PL011 UART sink

`microfmt/hw/pl011_sink.hpp` provides `microfmt::pl011_sink`, a controller for
the ARM PrimeCell PL011 UART, commonly memory-mapped at `0x09000000` on the
QEMU `virt` machine and on many ARM development boards:

```cpp
#include <microfmt/hw/pl011_sink.hpp>

microfmt::pl011_sink uart(0x09000000, /*translate_crlf=*/true);
uart.enable(); // optional: sets UARTEN/TXE/RXE
microfmt::format_to(uart.as_sink(), "EL{} boot\n", current_el);
```

`translate_crlf` expands `\n` to `\r\n` for terminal compatibility. Writes
busy-wait on the Flag Register's TXFF bit before pushing each byte into the
transmit FIFO, issuing a `yield` hint on AArch64/ARM while spinning.

## ARM semihosting sink

`microfmt/hw/semihosting.hpp` provides `microfmt::semihosting_sink`, which
routes formatted output to the host via the ARM semihosting `SYS_WRITEC`
operation (trapped with `hlt #0xf000` on AArch64 and `bkpt #0xab` on
AArch32/Thumb):

```cpp
#include <microfmt/hw/semihosting.hpp>

microfmt::semihosting_sink semihosting;
microfmt::format_to(semihosting.as_sink(), "Hello via semihosting\n");
```

This sink requires a semihosting-capable debug host or emulator (for example
`qemu-system-aarch64 -semihosting-config enable=on,target=native`) and is
intended for early boot diagnostics rather than production output.

## Debug-probe and generic UART sinks

On a real board you often have no console at all, or a UART that differs from
the PL011. These sinks cover that. All of them never block forever: when the
destination is stuck they drop the unsent bytes and count them in
`dropped_bytes()`, so a missing probe or unplugged cable cannot hang firmware.

Abbreviations: RTT = SEGGER Real-Time Transfer; ITM = Instrumentation Trace
Macrocell; SWO = Serial Wire Output (the trace pin); FIFO = first-in
first-out queue; CDC = USB Communications Device Class (virtual serial port).

| Sink | Header | What it is for |
|---|---|---|
| `rtt_sink<BufferSize>` | `microfmt/hw/rtt_sink.hpp` | Text over the debug probe through a ring buffer in RAM; no pins, tiny CPU cost. |
| `itm_sink` | `microfmt/hw/itm_sink.hpp` | Cortex-M ITM stimulus port, captured from the SWO pin by the probe. |
| `uart_sink` | `microfmt/hw/uart_sink.hpp` | Any polled UART; you supply "ready to send?" and "send byte" callbacks. |
| `chunked_sink<ChunkSize>` | `microfmt/sinks/chunked_sink.hpp` | Packet transports such as USB CDC (64-byte packets); call `flush()` to send a short packet. |

```cpp
static microfmt::rtt_sink<1024> rtt; // must stay at a fixed RAM address
microfmt::format_to(rtt.as_sink(), "adc={}\n", sample);

microfmt::itm_sink itm(microfmt::itm_sink::port_address(0));
microfmt::format_to(itm.as_sink(), "tick {}\n", ticks);
```

`rtt_sink` uses SEGGER's control-block layout (id `"SEGGER RTT"`), so J-Link
tools find it by scanning RAM; keep it in static storage and never move it.
`itm_sink` does not enable ITM/SWO itself: configure the trace clock and
stimulus port mask in your startup code first.

To feed the logging API from any of these, wrap the sink with
`microfmt::log::sink_log_adapter<Capacity>` from `<microfmt/log/sink_adapter.hpp>`;
it renders each record as one `[logger] payload` line:

```cpp
microfmt::log::sink_log_adapter<128> to_rtt(rtt.as_sink());
auto log_sink = to_rtt.as_sink(); // a microfmt::log::log_sink
```
