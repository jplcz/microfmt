# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.20)
project(jplcz_microfmt_cpm_consumer LANGUAGES CXX)

if(NOT DEFINED CPM_CMAKE_PATH)
    message(FATAL_ERROR "CPM_CMAKE_PATH must identify CPM.cmake")
endif()
if(NOT DEFINED JPLCZ_MICROFMT_SOURCE_DIR)
    message(FATAL_ERROR
        "JPLCZ_MICROFMT_SOURCE_DIR must identify the project checkout")
endif()

include("${CPM_CMAKE_PATH}")
CPMAddPackage(
    NAME jplcz_microfmt
    SOURCE_DIR "${JPLCZ_MICROFMT_SOURCE_DIR}"
    EXCLUDE_FROM_ALL YES
    OPTIONS
        "JPLCZ_MICROFMT_BUILD_TESTS OFF"
        "JPLCZ_MICROFMT_BUILD_EXAMPLES OFF"
        "JPLCZ_MICROFMT_BUILD_BENCHMARKS OFF"
        "JPLCZ_MICROFMT_BUILD_HEADER_CHECKS OFF"
        "JPLCZ_MICROFMT_INSTALL OFF"
)

add_executable(cpm_consumer main.cpp)
target_link_libraries(cpm_consumer PRIVATE jplcz_microfmt::microfmt)
