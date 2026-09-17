# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.20)
project(jplcz_microfmt_add_subdirectory_consumer LANGUAGES CXX)

if(NOT JPLCZ_MICROFMT_SOURCE_DIR)
    message(FATAL_ERROR "JPLCZ_MICROFMT_SOURCE_DIR is required")
endif()

set(CMAKE_CXX_STANDARD 23)
add_subdirectory("${JPLCZ_MICROFMT_SOURCE_DIR}" jplcz_microfmt)

if(NOT CMAKE_CXX_STANDARD EQUAL 23)
    message(FATAL_ERROR "jplcz_microfmt changed the parent C++ standard")
endif()

foreach(DEVELOPMENT_OPTION
        JPLCZ_MICROFMT_BUILD_TESTS
        JPLCZ_MICROFMT_BUILD_EXAMPLES
        JPLCZ_MICROFMT_BUILD_BENCHMARKS
        JPLCZ_MICROFMT_BUILD_HEADER_CHECKS
        JPLCZ_MICROFMT_ENABLE_STRICT_WARNINGS
        JPLCZ_MICROFMT_INSTALL)
    if(${DEVELOPMENT_OPTION})
        message(FATAL_ERROR
            "Unexpected embedded-build option: ${DEVELOPMENT_OPTION}")
    endif()
endforeach()

foreach(DEVELOPMENT_TARGET
        jplcz_microfmt_tests
        jplcz_microfmt_check_public_headers
        memory_scanner_demo
        bench_stack_painting)
    if(TARGET "${DEVELOPMENT_TARGET}")
        message(FATAL_ERROR
            "Unexpected embedded-build target: ${DEVELOPMENT_TARGET}")
    endif()
endforeach()

add_executable(add_subdirectory_consumer main.cpp)
target_link_libraries(add_subdirectory_consumer
    PRIVATE jplcz_microfmt::microfmt)
