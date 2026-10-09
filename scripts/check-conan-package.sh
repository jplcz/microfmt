#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly source_dir="$(cd -- "${script_dir}/.." && pwd)"
readonly conan_home="${JPLCZ_MICROFMT_CONAN_HOME:-${source_dir}/build/conan-home}"

if ! command -v conan >/dev/null 2>&1; then
  printf 'error: Conan 2 is required; install it with: python -m pip install conan\n' >&2
  exit 1
fi

export CONAN_HOME="${conan_home}"
mkdir -p "${CONAN_HOME}"

profile_detected=1
if ! conan profile detect --force; then
  profile_detected=0
fi

if [[ -n "${JPLCZ_MICROFMT_CONAN_COMPILER_VERSION:-}" ]]; then
  profile_path="${CONAN_HOME}/profiles/default"
  if [[ ! -f "${profile_path}" ]]; then
    printf 'error: Conan did not create the detected default profile\n' >&2
    exit 1
  fi
  temporary_profile="${profile_path}.tmp"
  awk \
    -v version="${JPLCZ_MICROFMT_CONAN_COMPILER_VERSION}" \
    '/^compiler\.version=/ {$0 = "compiler.version=" version} {print}' \
    "${profile_path}" >"${temporary_profile}"
  mv "${temporary_profile}" "${profile_path}"
elif ((profile_detected == 0)); then
  exit 1
fi

# microfmt depends on jplcz_reloco, which has no public package index entry: use a local checkout
# (JPLCZ_MICROFMT_RELOCO_SOURCE_DIR) or clone the repository.
reloco_dir="${JPLCZ_MICROFMT_RELOCO_SOURCE_DIR:-}"
if [[ -z "${reloco_dir}" ]]; then
  reloco_dir="${source_dir}/build/reloco-src"
  if [[ ! -d "${reloco_dir}/.git" ]]; then
    rm -rf "${reloco_dir}"
    git clone --depth 1 "${JPLCZ_MICROFMT_RELOCO_GIT_REPOSITORY:-https://github.com/jplcz/reloco.git}" "${reloco_dir}"
  fi
fi

conan create "${reloco_dir}" --build=missing -s compiler.cppstd=17 \
  ${JPLCZ_MICROFMT_CONAN_COMPILER_VERSION:+-s "compiler.version=${JPLCZ_MICROFMT_CONAN_COMPILER_VERSION}"}

create_args=(
  "${source_dir}"
  --build=missing
  -s compiler.cppstd=17
)

if [[ -n "${JPLCZ_MICROFMT_CONAN_COMPILER_VERSION:-}" ]]; then
  create_args+=(
    -s "compiler.version=${JPLCZ_MICROFMT_CONAN_COMPILER_VERSION}"
  )
fi

conan create "${create_args[@]}" "$@"
