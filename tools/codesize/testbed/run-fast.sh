#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Fast/standard testbed sweep for routine (e.g. AI-agent) iteration: only
# g++ at -Os, but with multiso mode (naive vs. MICROFMT_SHARED common .so)
# still enabled, since that's the scenario most changes in this area are
# meant to affect. See run.sh's own top-of-file comment for the full set of
# TESTBED_* knobs and what each build "category" measures.
#
# This intentionally skips the g++/clang++ x -O0/-Os/-O2/-O3 x
# runtime/static full sweep run.sh does by default -- use run.sh directly
# (optionally with TESTBED_* overrides) for a release-quality/exhaustive
# measurement; use this script for a quick sanity check after a code change.
#
# Usage: tools/codesize/testbed/run-fast.sh [--keep-build]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export TESTBED_TOOLCHAINS="${TESTBED_TOOLCHAINS:-g++}"
export TESTBED_OPT_LEVELS="${TESTBED_OPT_LEVELS:--Os}"
export TESTBED_STRING_MODES="${TESTBED_STRING_MODES:-runtime static}"
export TESTBED_RUN_MULTISO="${TESTBED_RUN_MULTISO:-1}"

exec "${SCRIPT_DIR}/run.sh" "$@"
