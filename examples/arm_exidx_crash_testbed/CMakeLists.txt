# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Standalone ARM EXIDX crash-unwind testbed.
#
# This is intentionally NOT added via add_subdirectory() from the main
# project (see ../../CMakeLists.txt) and is not built by the default
# jplcz_microfmt build: it targets 32-bit ARM Linux userspace and requires a
# cross-toolchain plus qemu-user, neither of which the main library build
# depends on. Configure and build it as its own project:
#
#   cmake -S . -B build-arm -DCMAKE_TOOLCHAIN_FILE=toolchain-arm-linux-gnueabi.cmake
#   cmake --build build-arm
#   qemu-arm -static build-arm/arm_exidx_crash_testbed   # (or -L <sysroot> if not static)
#
# See README.md for prerequisites and .vscode/launch.json for one-click
# build+debug support in VS Code via qemu-arm's GDB stub.

cmake_minimum_required(VERSION 3.20)
project(arm_exidx_crash_testbed CXX)

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
  message(
    FATAL_ERROR
      "This testbed targets 32-bit ARM only. Configure with "
      "-DCMAKE_TOOLCHAIN_FILE=toolchain-arm-linux-gnueabi.cmake (see README.md).")
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE
      Debug
      CACHE STRING "Build type" FORCE)
endif()

add_executable(arm_exidx_crash_testbed main.cpp)
target_include_directories(arm_exidx_crash_testbed PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
target_compile_features(arm_exidx_crash_testbed PRIVATE cxx_std_20)
# -fno-exceptions: main.cpp deliberately places non-trivial C++ objects
# (std::string/std::vector/std::unique_ptr members, a virtual destructor) as
# locals in the crash-path frames to exercise realistic stack layouts. With
# exceptions enabled, GCC gives these functions a *real* personality routine
# (e.g. __gxx_personality_v0, an out-of-line function pointer plus LSDA) so
# their partially-constructed locals can be cleaned up if a throw happens --
# a fundamentally different, still-unsupported EHABI encoding (see the
# "Personality routine: 0x..." readelf --unwind form, as opposed to the
# compact-model / personality-0/1/2 generic-model bytecode this unwinder
# implements). Building without exceptions keeps every frame on the
# supported bytecode formats while still using genuinely complex objects.
target_compile_options(arm_exidx_crash_testbed PRIVATE -Wall -Wextra -g -O0 -funwind-tables -fno-exceptions)
target_link_options(arm_exidx_crash_testbed PRIVATE -Wl,--export-dynamic)

# TESTBED_THUMB=ON compiles the whole binary as Thumb/Thumb-2 (-mthumb)
# instead of the default 32-bit ARM (A32) instruction set, exercising the R7
# Thumb frame-pointer path (arm_abi_traits::resolve_fp_reg, CPSR.T) rather
# than the default R11/A32 path. Both configurations should unwind
# identically; compare backtraces from the two builds to check Thumb support:
#
#   ./build.sh -DTESTBED_THUMB=ON
option(TESTBED_THUMB "Compile as Thumb/Thumb-2 (-mthumb) instead of ARM (A32)" OFF)
if(TESTBED_THUMB)
  target_compile_options(arm_exidx_crash_testbed PRIVATE -mthumb -mthumb-interwork)
endif()