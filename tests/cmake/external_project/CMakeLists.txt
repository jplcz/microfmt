# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.20)
project(jplcz_microfmt_external_project_consumer LANGUAGES CXX)

include(ExternalProject)

if(NOT JPLCZ_MICROFMT_SOURCE_DIR)
    message(FATAL_ERROR "JPLCZ_MICROFMT_SOURCE_DIR is required")
endif()

ExternalProject_Add(
    jplcz_microfmt_external
    SOURCE_DIR "${JPLCZ_MICROFMT_SOURCE_DIR}"
    BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/jplcz_microfmt-build"
    INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/prefix"
    CMAKE_ARGS
        "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
        "-DJPLCZ_MICROFMT_INSTALL=ON"
        "-DJPLCZ_MICROFMT_BUILD_TESTS=OFF"
        "-DJPLCZ_MICROFMT_BUILD_EXAMPLES=OFF"
        "-DJPLCZ_MICROFMT_BUILD_BENCHMARKS=OFF"
        "-DJPLCZ_MICROFMT_BUILD_HEADER_CHECKS=OFF"
)
