# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.20)
project(microfmt_external_project_consumer LANGUAGES CXX)

include(ExternalProject)

if(NOT MICROFMT_SOURCE_DIR)
    message(FATAL_ERROR "MICROFMT_SOURCE_DIR is required")
endif()

ExternalProject_Add(
    microfmt_external
    SOURCE_DIR "${MICROFMT_SOURCE_DIR}"
    BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/microfmt-build"
    INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/prefix"
    CMAKE_ARGS
        "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
        "-DMICROFMT_INSTALL=ON"
        "-DMICROFMT_BUILD_TESTS=OFF"
        "-DMICROFMT_BUILD_EXAMPLES=OFF"
        "-DMICROFMT_BUILD_BENCHMARKS=OFF"
        "-DMICROFMT_BUILD_HEADER_CHECKS=OFF"
)
