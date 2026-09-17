#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly source_dir="$(cd -- "${script_dir}/.." && pwd)"

if ! command -v doxygen >/dev/null 2>&1; then
  printf 'error: Doxygen is required to build the API documentation\n' >&2
  exit 1
fi

mkdir -p "${source_dir}/build/docs"
cd "${source_dir}"
doxygen Doxyfile
