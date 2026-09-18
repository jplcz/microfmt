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
