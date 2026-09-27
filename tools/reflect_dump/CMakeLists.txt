# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

# Standalone demo/smoke-test for tools/reflect_dump and
# cmake/ReflectDump.cmake. Deliberately *not* part of the main
# jplcz_microfmt build (never add_subdirectory()'d from the top-level
# CMakeLists.txt, no JPLCZ_MICROFMT_BUILD_* option controls it): it needs
# its own, separately configured build tree, has its own compiler-support
# requirements (a P2996 `-freflection` host compiler must exist somewhere
# on the machine, even though the resulting demo binary itself never
# needs one), and exists purely to prove the tool/macro works end to end,
# not to ship as part of the library. Configure and build it on its own:
#
#   cmake -S tools/reflect_dump -B build-reflect-dump \
#       -DJPLCZ_MICROFMT_REFLECT_DUMP_COMPILER=g++-16 \
#       -DJPLCZ_MICROFMT_REFLECT_DUMP_RELOCO_INCLUDE_DIR=/path/to/reloco/include
#   cmake --build build-reflect-dump
#   ./build-reflect-dump/reflect_dump_demo
#
# The demo executable itself is compiled by this project's own, ordinary
# CMAKE_CXX_COMPILER (whatever that is -- including a cross-compiler): it
# only consumes the generated, reflection-free header, so it has none of
# the P2996 requirements the generator step above does. That generator
# step instead always runs on/for the host machine performing the build,
# controlled entirely by JPLCZ_MICROFMT_REFLECT_DUMP_COMPILER below, and
# is completely independent of this project()'s own CXX toolchain -- see
# cmake/ReflectDump.cmake's header comment for why.
cmake_minimum_required(VERSION 3.20)
project(jplcz_microfmt_reflect_dump_demo CXX)

set(JPLCZ_MICROFMT_REFLECT_DUMP_COMPILER "" CACHE STRING
    "Path or name of a P2996-capable (-freflection) HOST compiler used to \
build and run the reflect_dump generator. Required, never auto-detected \
-- see cmake/ReflectDump.cmake.")
if(NOT JPLCZ_MICROFMT_REFLECT_DUMP_COMPILER)
    message(FATAL_ERROR
        "-DJPLCZ_MICROFMT_REFLECT_DUMP_COMPILER=<compiler> is required, e.g. "
        "-DJPLCZ_MICROFMT_REFLECT_DUMP_COMPILER=g++-16. This demo never "
        "guesses a compiler for you.")
endif()

set(JPLCZ_MICROFMT_REFLECT_DUMP_MICROFMT_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include" CACHE PATH
    "microfmt include/ directory used by the reflect_dump generator")

set(JPLCZ_MICROFMT_REFLECT_DUMP_RELOCO_INCLUDE_DIR "" CACHE PATH
    "jplcz_reloco include/ directory used by the reflect_dump generator. \
Required, never auto-detected/fetched -- point it at any existing reloco \
checkout's include/ directory.")
if(NOT JPLCZ_MICROFMT_REFLECT_DUMP_RELOCO_INCLUDE_DIR)
    message(FATAL_ERROR
        "-DJPLCZ_MICROFMT_REFLECT_DUMP_RELOCO_INCLUDE_DIR=<dir> is required, "
        "pointing at a jplcz_reloco checkout's include/ directory.")
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/ReflectDump.cmake")

set(JPLCZ_MICROFMT_REFLECT_DUMP_DEMO_OUTPUT
    "${CMAKE_CURRENT_BINARY_DIR}/generated/reflect_dump_demo_generated.hpp")

jplcz_microfmt_add_reflect_dump(reflect_dump_demo_generate
    COMPILER "${JPLCZ_MICROFMT_REFLECT_DUMP_COMPILER}"
    MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/example_manifest.hpp"
    OUTPUT "${JPLCZ_MICROFMT_REFLECT_DUMP_DEMO_OUTPUT}"
    INCLUDE_DIRS
        "${JPLCZ_MICROFMT_REFLECT_DUMP_MICROFMT_INCLUDE_DIR}"
        "${JPLCZ_MICROFMT_REFLECT_DUMP_RELOCO_INCLUDE_DIR}"
)

add_executable(reflect_dump_demo demo/reflect_dump_demo.cpp)
add_dependencies(reflect_dump_demo reflect_dump_demo_generate)
target_compile_features(reflect_dump_demo PRIVATE cxx_std_20)
target_include_directories(reflect_dump_demo PRIVATE
    "${JPLCZ_MICROFMT_REFLECT_DUMP_MICROFMT_INCLUDE_DIR}"
    "${JPLCZ_MICROFMT_REFLECT_DUMP_RELOCO_INCLUDE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

enable_testing()
add_test(NAME reflect_dump_demo COMMAND reflect_dump_demo)
