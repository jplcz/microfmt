#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

set -uo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly source_dir="$(cd -- "${script_dir}/.." && pwd)"
readonly compiler="${MICROFMT_UNSAFE_BUFFER_CXX:-clang++-24}"
readonly max_warnings="${MICROFMT_UNSAFE_BUFFER_MAX_WARNINGS:-0}"
readonly reloco_include="${MICROFMT_UNSAFE_BUFFER_RELOCO_INCLUDE:-${source_dir}/../reloco/include}"
read -r -a standards <<<"${MICROFMT_UNSAFE_BUFFER_STANDARDS:-17 20 23}"

if [[ ! -d "${reloco_include}" ]]; then
  printf 'error: reloco include directory not found: %s\n' "${reloco_include}" >&2
  printf 'Set MICROFMT_UNSAFE_BUFFER_RELOCO_INCLUDE to a checkout of https://github.com/jplcz/reloco\n' >&2
  exit 1
fi

if ! command -v "${compiler}" >/dev/null 2>&1; then
  printf 'error: Clang 24 compiler not found: %s\n' "${compiler}" >&2
  exit 1
fi

if ! "${compiler}" --version | head -n 1 | grep -Eq 'clang version 24(\.| )'; then
  printf 'error: %s is not Clang 24\n' "${compiler}" >&2
  "${compiler}" --version | head -n 1 >&2
  exit 1
fi

failures=()

for standard in "${standards[@]}"; do
  log_file="${TMPDIR:-/tmp}/microfmt-unsafe-buffer-cxx${standard}.$$.log"

  printf '==> Checking C++%s with %s\n' "${standard}" "${compiler}"
  if ! "${compiler}" \
      "-std=c++${standard}" \
      "-DMICROFMT_HEADER_CHECK_STANDARD=${standard}" \
      -I"${source_dir}/include" \
      -I"${reloco_include}" \
      -Wunsafe-buffer-usage \
      -fsyntax-only \
      "${source_dir}/tests/compile_all_headers.cpp" \
      >"${log_file}" 2>&1; then
    cat "${log_file}" >&2
    rm -f "${log_file}"
    failures+=("C++${standard}: compilation failed")
    continue
  fi

  warning_count="$(grep -c 'warning:' "${log_file}" || true)"
  printf '    unsafe-buffer diagnostics: %s (maximum: %s)\n' \
    "${warning_count}" "${max_warnings}"

  grep -E '^.*/include/microfmt/[^:]+:[0-9]+:[0-9]+: warning:' \
      "${log_file}" |
    sed -E 's/:([0-9]+):([0-9]+): warning:.*//' |
    sort |
    uniq -c |
    sort -nr || true

  printf '    full diagnostic report:\n'
  cat "${log_file}"

  if ((warning_count > max_warnings)); then
    failures+=(
      "C++${standard}: ${warning_count} warnings exceed ${max_warnings}"
    )
  fi

  rm -f "${log_file}"
done

if ((${#failures[@]} != 0)); then
  printf '\nUnsafe-buffer analysis failed:\n' >&2
  printf '  - %s\n' "${failures[@]}" >&2
  exit 1
fi

printf '\nUnsafe-buffer diagnostic count is within the migration baseline.\n'
