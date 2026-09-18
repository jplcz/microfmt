#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly source_dir="$(cd -- "${script_dir}/.." && pwd)"
readonly build_dir="${JPLCZ_MICROFMT_CPACK_BUILD_DIR:-${source_dir}/build/cpack}"
readonly parallel="${JPLCZ_MICROFMT_BUILD_PARALLEL:-2}"

cmake \
  -S "${source_dir}" \
  -B "${build_dir}" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DJPLCZ_MICROFMT_BUILD_TESTS=OFF \
  -DJPLCZ_MICROFMT_BUILD_EXAMPLES=OFF \
  -DJPLCZ_MICROFMT_BUILD_BENCHMARKS=OFF \
  -DJPLCZ_MICROFMT_BUILD_HEADER_CHECKS=OFF \
  -DJPLCZ_MICROFMT_INSTALL=ON \
  "$@"

cmake --build "${build_dir}" --parallel "${parallel}"

(cd "${build_dir}" && cpack)

shopt -s nullglob
packages=("${build_dir}"/jplcz-microfmt-*.tar.gz "${build_dir}"/jplcz-microfmt-*.zip \
  "${build_dir}"/jplcz-microfmt*.deb "${build_dir}"/jplcz-microfmt-*.rpm)
shopt -u nullglob

if [[ ${#packages[@]} -eq 0 ]]; then
  printf 'error: cpack did not produce any package\n' >&2
  exit 1
fi

printf 'Generated packages:\n'
printf '  %s\n' "${packages[@]}"

archive="${build_dir}/jplcz-microfmt-0.1.0-Linux.tar.gz"
if [[ ! -f "${archive}" ]]; then
  printf 'error: expected archive package %s was not generated\n' "${archive}" >&2
  exit 1
fi

extract_dir="${build_dir}/extracted"
rm -rf "${extract_dir}"
mkdir -p "${extract_dir}"
tar -xzf "${archive}" -C "${extract_dir}"

package_root="$(find "${extract_dir}" -mindepth 1 -maxdepth 1 -type d)"

for required in \
  "include/microfmt/microfmt.hpp" \
  "lib/cmake/jplcz_microfmt/jplcz_microfmtConfig.cmake" \
  "lib/cmake/jplcz_microfmt/jplcz_microfmtConfigVersion.cmake" \
  "lib/cmake/jplcz_microfmt/jplcz_microfmtTargets.cmake"; do
  if [[ ! -f "${package_root}/${required}" ]]; then
    printf 'error: archive package is missing %s\n' "${required}" >&2
    exit 1
  fi
done

printf 'Archive package contents verified under %s\n' "${package_root}"
