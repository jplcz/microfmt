#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

# One-click configure + build + run for tools/reflect_dump's standalone
# CMake demo (see CMakeLists.txt / README.md's "CMake integration"
# section). Deliberately requires the reflection-capable host compiler
# and jplcz_reloco include directory to be given explicitly -- this
# script never guesses either, for the same reason
# cmake/ReflectDump.cmake's COMPILER argument never does.
#
# Usage:
#   ./run_demo.sh <path-or-name-of-freflection-compiler> <reloco-include-dir> [extra cmake args...]
#
# Example:
#   ./run_demo.sh g++-16 /path/to/reloco/include
#   ./run_demo.sh g++-16 /path/to/reloco/include -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain.cmake

set -euo pipefail

# NOTE: this script runs the resulting reflect_dump_demo binary directly
# on this machine, so it only makes sense for a native (non-cross)
# configure. If you pass -DCMAKE_TOOLCHAIN_FILE=... to cross-compile the
# demo, configure/build succeed (the generator step still runs on the
# host regardless, see cmake/ReflectDump.cmake) but you must run the
# resulting binary yourself (e.g. via qemu-user or on real target
# hardware) instead of relying on this script's final "run" step.

if [[ $# -lt 2 ]]; then
    echo "usage: $0 <path-or-name-of-freflection-compiler> <reloco-include-dir> [extra cmake args...]" >&2
    exit 1
fi

reflect_dump_compiler="$1"
reloco_include_dir="$2"
shift 2

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${script_dir}/build-reflect-dump-demo"

cmake -S "${script_dir}" -B "${build_dir}" \
    -DJPLCZ_MICROFMT_REFLECT_DUMP_COMPILER="${reflect_dump_compiler}" \
    -DJPLCZ_MICROFMT_REFLECT_DUMP_RELOCO_INCLUDE_DIR="${reloco_include_dir}" \
    "$@"

cmake --build "${build_dir}"

echo
echo "=== running reflect_dump_demo ==="
"${build_dir}/reflect_dump_demo"
