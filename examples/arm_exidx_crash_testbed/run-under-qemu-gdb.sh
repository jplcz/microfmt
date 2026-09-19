#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Runs the cross-compiled testbed binary under qemu-arm's GDB stub so a
# debugger (VS Code's launch.json in .vscode/, or `gdb-multiarch` by hand)
# can attach to it over TCP.
#
# Usage: ./run-under-qemu-gdb.sh [port] [path-to-binary]
#
# qemu-arm halts the emulated CPU immediately and waits for a debugger to
# connect on the given port before executing a single instruction, so run
# this first, then attach.

set -euo pipefail

PORT="${1:-1234}"
BINARY="${2:-$(dirname "$0")/build-arm/arm_exidx_crash_testbed}"

if [[ ! -x "$BINARY" ]]; then
  echo "error: testbed binary not found or not executable: $BINARY" >&2
  echo "Build it first, e.g.:" >&2
  echo "  cmake -S . -B build-arm -DCMAKE_TOOLCHAIN_FILE=toolchain-arm-linux-gnueabi.cmake" >&2
  echo "  cmake --build build-arm" >&2
  exit 1
fi

if ! command -v qemu-arm >/dev/null 2>&1; then
  echo "error: qemu-arm not found on PATH (install the 'qemu-user' package)." >&2
  exit 1
fi

echo "Waiting for a debugger to connect on 127.0.0.1:$PORT ..."
exec qemu-arm -g "$PORT" "$BINARY"
