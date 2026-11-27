#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause
"""Extract a core file from a captured microfmt::coredump::framed_sink stream.

Why: a core dump sent over a serial or RTT link is wrapped in checksummed frames
(see include/microfmt/coredump/framed_sink.hpp) and may be mixed with log text.
This tool finds the frames, checks every checksum and writes the payload bytes
to a core file that gdb can open.

Usage: coredump_unframe.py CAPTURE OUTPUT_CORE
Exit status is 0 only if the end frame was found and every byte is accounted for.
"""
import struct
import sys

SYNC = b"\xa5\x5a"
DATA, END = 1, 2


def crc16(data: bytes, crc: int = 0xFFFF) -> int:
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def unframe(stream: bytes):
    """Returns (core_bytes, ok, problems)."""
    core = bytearray()
    problems = []
    pos = 0
    while True:
        pos = stream.find(SYNC, pos)
        if pos < 0:
            problems.append("end frame not found")
            return bytes(core), False, problems
        if pos + 5 > len(stream):
            problems.append("truncated frame header")
            return bytes(core), False, problems
        ftype, length = struct.unpack_from("<BH", stream, pos + 2)
        end = pos + 5 + length + 2
        if end > len(stream) or ftype not in (DATA, END):
            pos += 1  # a sync pattern inside other data; keep scanning
            continue
        body = stream[pos + 2 : pos + 5 + length]
        (crc,) = struct.unpack_from("<H", stream, pos + 5 + length)
        if crc16(body) != crc:
            problems.append(f"bad checksum at stream offset {pos}")
            pos += 1
            continue
        payload = stream[pos + 5 : pos + 5 + length]
        if ftype == DATA:
            core += payload
        else:
            (total,) = struct.unpack("<I", payload)
            if total != len(core):
                problems.append(f"expected {total} bytes but recovered {len(core)}")
                return bytes(core), False, problems
            return bytes(core), not problems, problems
        pos = end


def main(argv) -> int:
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    with open(argv[1], "rb") as capture:
        core, ok, problems = unframe(capture.read())
    with open(argv[2], "wb") as output:
        output.write(core)
    for problem in problems:
        print("warning:", problem, file=sys.stderr)
    print(f"wrote {len(core)} bytes", file=sys.stderr)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
