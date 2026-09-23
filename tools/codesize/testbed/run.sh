#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Thin wrapper kept for backwards compatibility / muscle memory: the
# actual testbed driver is orchestrate.py, a pure-Python dependency-graph
# executor (no external build-system dependency -- only python3, which
# this testbed already requires for generate_modules.py and measure.py).
# See orchestrate.py's own module docstring for the full list of
# supported environment variables and the --keep-build flag.
#
# Usage: tools/codesize/testbed/run.sh [--keep-build]
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "${SCRIPT_DIR}/orchestrate.py" "$@"
