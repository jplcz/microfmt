# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause
#
# CMake toolchain file for cross-compiling this testbed to 32-bit ARM Linux
# userspace (soft-float EABI) using the arm-linux-gnueabi-* cross toolchain
# (Debian/Ubuntu packages: gcc-arm-linux-gnueabi g++-arm-linux-gnueabi).
#
# Usage:
#   cmake -S . -B build-arm -DCMAKE_TOOLCHAIN_FILE=toolchain-arm-linux-gnueabi.cmake
#   cmake --build build-arm
#
# Override MICROFMT_ARM_TOOLCHAIN_PREFIX to point at a differently-prefixed
# cross toolchain (e.g. arm-linux-gnueabihf- for the hard-float ABI):
#   cmake -S . -B build-arm \
#     -DCMAKE_TOOLCHAIN_FILE=toolchain-arm-linux-gnueabi.cmake \
#     -DMICROFMT_ARM_TOOLCHAIN_PREFIX=arm-linux-gnueabihf-

set(MICROFMT_ARM_TOOLCHAIN_PREFIX
    "arm-linux-gnueabi-"
    CACHE STRING "Cross-toolchain binutils/gcc prefix")

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER "${MICROFMT_ARM_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${MICROFMT_ARM_TOOLCHAIN_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${MICROFMT_ARM_TOOLCHAIN_PREFIX}gcc")

# Only search for libraries/headers within the cross sysroot, but allow
# looking up host programs (e.g. CMake itself, git) normally.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Statically linked binaries need no target sysroot/loader present on the
# host to run under `qemu-arm`, which is what this testbed builds by default
# (see CMakeLists.txt); CMAKE_TRY_COMPILE_TARGET_TYPE avoids a failing
# CMAKE_CXX_COMPILER `try_compile` check under `-static` before libc is
# fully wired up.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
