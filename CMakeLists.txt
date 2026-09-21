# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.20)
project(jplcz_microfmt VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(JPLCZ_MICROFMT_IS_TOP_LEVEL ON)
else()
    set(JPLCZ_MICROFMT_IS_TOP_LEVEL OFF)
endif()

set(JPLCZ_MICROFMT_DEVELOPMENT_DEFAULT ${JPLCZ_MICROFMT_IS_TOP_LEVEL})

option(JPLCZ_MICROFMT_ENABLE_STRICT_WARNINGS
    "Enable comprehensive GCC and Clang warnings" ${JPLCZ_MICROFMT_DEVELOPMENT_DEFAULT})
option(JPLCZ_MICROFMT_ENABLE_UNSAFE_BUFFER_USAGE
    "Enable Clang unsafe buffer usage diagnostics" OFF)
option(JPLCZ_MICROFMT_ENABLE_WERROR
    "Enable build with -Werror" OFF)
option(JPLCZ_MICROFMT_BUILD_TESTS
    "Build test suite" ${JPLCZ_MICROFMT_DEVELOPMENT_DEFAULT})
option(JPLCZ_MICROFMT_BUILD_EXAMPLES
    "Build example apps" ${JPLCZ_MICROFMT_DEVELOPMENT_DEFAULT})
option(JPLCZ_MICROFMT_BUILD_BENCHMARKS
    "Build benchmark apps" ${JPLCZ_MICROFMT_DEVELOPMENT_DEFAULT})
option(JPLCZ_MICROFMT_BUILD_HEADER_CHECKS
    "Compile all public headers under each supported C++ standard"
    ${JPLCZ_MICROFMT_DEVELOPMENT_DEFAULT})
option(JPLCZ_MICROFMT_INSTALL
    "Install jplcz_microfmt headers and CMake package metadata"
    ${JPLCZ_MICROFMT_IS_TOP_LEVEL})
option(JPLCZ_MICROFMT_BUILD_MANPAGES
    "Generate man(7) pages from the Markdown docs using pandoc, when available"
    OFF)

if(JPLCZ_MICROFMT_BUILD_TESTS OR JPLCZ_MICROFMT_BUILD_EXAMPLES OR
   JPLCZ_MICROFMT_BUILD_HEADER_CHECKS)
    include(CheckIncludeFileCXX)
    check_include_file_cxx(syslog.h JPLCZ_MICROFMT_HAS_SYSLOG)

    find_package(Boost 1.77 CONFIG QUIET)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(JPLCZ_MICROFMT_SYSTEMD QUIET IMPORTED_TARGET libsystemd)
        pkg_check_modules(JPLCZ_MICROFMT_TIZEN_DLOG QUIET IMPORTED_TARGET dlog)
    endif()
endif()

if(JPLCZ_MICROFMT_ENABLE_STRICT_WARNINGS OR JPLCZ_MICROFMT_ENABLE_UNSAFE_BUFFER_USAGE OR
   JPLCZ_MICROFMT_BUILD_HEADER_CHECKS OR JPLCZ_MICROFMT_BUILD_TESTS)
    include(CheckCXXCompilerFlag)

    if(MSVC)
        check_cxx_compiler_flag("/std:c++latest"
            JPLCZ_MICROFMT_COMPILER_SUPPORTS_CXX23)
    else()
        check_cxx_compiler_flag("-std=c++23"
            JPLCZ_MICROFMT_COMPILER_SUPPORTS_CXX23)
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        check_cxx_compiler_flag("-Wdangling"
            JPLCZ_MICROFMT_COMPILER_SUPPORTS_WDANGLING)
        check_cxx_compiler_flag("-Wdangling-gsl"
            JPLCZ_MICROFMT_COMPILER_SUPPORTS_WDANGLING_GSL)
        check_cxx_compiler_flag("-Wdangling-assignment-gsl"
            JPLCZ_MICROFMT_COMPILER_SUPPORTS_WDANGLING_ASSIGNMENT_GSL)
        check_cxx_compiler_flag("-Wdangling-field"
            JPLCZ_MICROFMT_COMPILER_SUPPORTS_WDANGLING_FIELD)
        check_cxx_compiler_flag("-Wreturn-stack-address"
            JPLCZ_MICROFMT_COMPILER_SUPPORTS_WRETURN_STACK_ADDRESS)
        check_cxx_compiler_flag("-Wunsafe-buffer-usage"
            JPLCZ_MICROFMT_COMPILER_SUPPORTS_WUNSAFE_BUFFER_USAGE)
        check_cxx_compiler_flag("-Wconsumed"
            JPLCZ_MICROFMT_COMPILER_SUPPORTS_WCONSUMED)
    endif()
endif()

# jplcz_reloco dependency
#
# jplcz_reloco provides the hardened container / lifetime-safety primitives
# that jplcz_microfmt is migrating onto. Callers that already vendor or pin a
# specific reloco checkout can point JPLCZ_MICROFMT_RELOCO_SOURCE_DIR at it;
# otherwise it is fetched from its GitHub repository at a pinned commit.
set(JPLCZ_MICROFMT_RELOCO_SOURCE_DIR "" CACHE PATH
    "Path to a local jplcz_reloco checkout to use instead of fetching it")
set(JPLCZ_MICROFMT_RELOCO_GIT_REPOSITORY "https://github.com/jplcz/reloco.git" CACHE STRING
    "Git repository to fetch jplcz_reloco from when JPLCZ_MICROFMT_RELOCO_SOURCE_DIR is unset")
set(JPLCZ_MICROFMT_RELOCO_GIT_TAG "master" CACHE STRING
    "Git tag or commit to fetch jplcz_reloco from when JPLCZ_MICROFMT_RELOCO_SOURCE_DIR is unset")

include(FetchContent)
if(JPLCZ_MICROFMT_RELOCO_SOURCE_DIR)
    FetchContent_Declare(
        jplcz_reloco
        SOURCE_DIR "${JPLCZ_MICROFMT_RELOCO_SOURCE_DIR}"
    )
else()
    FetchContent_Declare(
        jplcz_reloco
        GIT_REPOSITORY "${JPLCZ_MICROFMT_RELOCO_GIT_REPOSITORY}"
        GIT_TAG "${JPLCZ_MICROFMT_RELOCO_GIT_TAG}"
    )
endif()

# jplcz_reloco is consumed as a header-only build dependency: keep its own
# development targets (tests, header checks, strict warnings) out of the
# combined build, and mirror JPLCZ_MICROFMT_INSTALL so an installed
# jplcz_microfmt package also installs and exports the jplcz_reloco package it
# depends on.
set(JPLCZ_RELOCO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(JPLCZ_RELOCO_BUILD_HEADER_CHECKS OFF CACHE BOOL "" FORCE)
set(JPLCZ_RELOCO_ENABLE_STRICT_WARNINGS OFF CACHE BOOL "" FORCE)
set(JPLCZ_RELOCO_INSTALL ${JPLCZ_MICROFMT_INSTALL} CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(jplcz_reloco)

# Header-only interface target
add_library(jplcz_microfmt INTERFACE)
target_compile_features(jplcz_microfmt INTERFACE cxx_std_17)
target_compile_options(jplcz_microfmt INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>
)
set_target_properties(jplcz_microfmt PROPERTIES EXPORT_NAME microfmt)

target_include_directories(jplcz_microfmt INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(jplcz_microfmt INTERFACE jplcz_reloco::reloco)

add_library(jplcz_microfmt::microfmt ALIAS jplcz_microfmt)

add_library(jplcz_microfmt_warnings INTERFACE)
if(JPLCZ_MICROFMT_ENABLE_UNSAFE_BUFFER_USAGE AND
   NOT JPLCZ_MICROFMT_COMPILER_SUPPORTS_WUNSAFE_BUFFER_USAGE)
    message(FATAL_ERROR
        "JPLCZ_MICROFMT_ENABLE_UNSAFE_BUFFER_USAGE requires a compiler that supports "
        "-Wunsafe-buffer-usage")
endif()

if(JPLCZ_MICROFMT_ENABLE_STRICT_WARNINGS)
    target_compile_options(jplcz_microfmt_warnings INTERFACE
        $<$<CXX_COMPILER_ID:GNU>:
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wcast-align
            -Wcast-qual
            -Wundef
            -Wnull-dereference
            -Wdouble-promotion
            -Wimplicit-fallthrough
            -Woverloaded-virtual
            -Wnon-virtual-dtor
            -Wduplicated-cond
            -Wlogical-op
            -Wno-psabi
            $<$<BOOL:${JPLCZ_MICROFMT_ENABLE_WERROR}>:-Werror>
        >
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wcast-align
            -Wcast-qual
            -Wundef
            -Wnull-dereference
            -Wdouble-promotion
            -Wimplicit-fallthrough
            -Woverloaded-virtual
            -Wnon-virtual-dtor
            -Wextra-semi
            -Wimplicit-int-conversion
            -Wshorten-64-to-32
            $<$<BOOL:${JPLCZ_MICROFMT_COMPILER_SUPPORTS_WDANGLING}>:-Wdangling>
            $<$<BOOL:${JPLCZ_MICROFMT_COMPILER_SUPPORTS_WDANGLING_GSL}>:-Wdangling-gsl>
            $<$<BOOL:${JPLCZ_MICROFMT_COMPILER_SUPPORTS_WDANGLING_ASSIGNMENT_GSL}>:-Wdangling-assignment-gsl>
            $<$<BOOL:${JPLCZ_MICROFMT_COMPILER_SUPPORTS_WDANGLING_FIELD}>:-Wdangling-field>
            $<$<BOOL:${JPLCZ_MICROFMT_COMPILER_SUPPORTS_WRETURN_STACK_ADDRESS}>:-Wreturn-stack-address>
            $<$<BOOL:${JPLCZ_MICROFMT_COMPILER_SUPPORTS_WCONSUMED}>:-Wconsumed>
            $<$<BOOL:${JPLCZ_MICROFMT_ENABLE_WERROR}>:-Werror>
        >
    )
endif()

if(JPLCZ_MICROFMT_ENABLE_UNSAFE_BUFFER_USAGE)
    target_compile_options(jplcz_microfmt_warnings INTERFACE
        -Wunsafe-buffer-usage
        $<$<NOT:$<BOOL:${JPLCZ_MICROFMT_ENABLE_WERROR}>>:
            -Wno-error=unsafe-buffer-usage
        >
    )
endif()

function(jplcz_microfmt_enable_warnings TARGET)
    target_link_libraries(${TARGET} PRIVATE jplcz_microfmt_warnings)
endfunction()

if(JPLCZ_MICROFMT_BUILD_HEADER_CHECKS)
    function(add_jplcz_microfmt_header_check STANDARD)
        add_library(jplcz_microfmt_headers_cxx${STANDARD} OBJECT
            tests/compile_all_headers.cpp)
        target_link_libraries(jplcz_microfmt_headers_cxx${STANDARD} PRIVATE jplcz_microfmt)
        jplcz_microfmt_enable_warnings(jplcz_microfmt_headers_cxx${STANDARD})
        set_target_properties(jplcz_microfmt_headers_cxx${STANDARD} PROPERTIES
            CXX_STANDARD ${STANDARD}
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
        )
        target_compile_definitions(jplcz_microfmt_headers_cxx${STANDARD} PRIVATE
            MICROFMT_HEADER_CHECK_STANDARD=${STANDARD}
        )

        if(Boost_FOUND)
            target_link_libraries(jplcz_microfmt_headers_cxx${STANDARD} PRIVATE
                Boost::boost)
            target_compile_definitions(jplcz_microfmt_headers_cxx${STANDARD} PRIVATE
                MICROFMT_COMPILE_WITH_BOOST=1
                MICROFMT_COMPILE_WITH_BOOST_DESCRIBE=1
                MICROFMT_ENABLE_BOOST_UUID=1
                MICROFMT_ENABLE_BOOST_SOURCE_LOCATION=1)

            if(STANDARD GREATER_EQUAL 20)
                target_compile_definitions(jplcz_microfmt_headers_cxx${STANDARD} PRIVATE
                    MICROFMT_COMPILE_WITH_BOOST_ASIO=1)
            endif()
        endif()

        if(JPLCZ_MICROFMT_HAS_SYSLOG)
            target_compile_definitions(jplcz_microfmt_headers_cxx${STANDARD}
                PRIVATE MICROFMT_COMPILE_WITH_SYSLOG=1)
        endif()

        if(STANDARD GREATER_EQUAL 20 AND TARGET PkgConfig::JPLCZ_MICROFMT_SYSTEMD)
            target_link_libraries(jplcz_microfmt_headers_cxx${STANDARD} PRIVATE
                PkgConfig::JPLCZ_MICROFMT_SYSTEMD)
            target_compile_definitions(jplcz_microfmt_headers_cxx${STANDARD} PRIVATE
                MICROFMT_COMPILE_WITH_SYSTEMD=1)
        endif()

        if(STANDARD GREATER_EQUAL 20 AND TARGET PkgConfig::JPLCZ_MICROFMT_TIZEN_DLOG)
            target_link_libraries(jplcz_microfmt_headers_cxx${STANDARD} PRIVATE
                PkgConfig::JPLCZ_MICROFMT_TIZEN_DLOG)
            target_compile_definitions(jplcz_microfmt_headers_cxx${STANDARD} PRIVATE
                MICROFMT_COMPILE_WITH_TIZEN_DLOG=1)
        endif()
    endfunction()

    add_jplcz_microfmt_header_check(17)
    add_jplcz_microfmt_header_check(20)

    set(JPLCZ_MICROFMT_HEADER_CHECK_TARGETS
        jplcz_microfmt_headers_cxx17
        jplcz_microfmt_headers_cxx20
    )
    set(JPLCZ_MICROFMT_HAS_CXX23_WRAPPER FALSE)
    if(JPLCZ_MICROFMT_COMPILER_SUPPORTS_CXX23)
        add_jplcz_microfmt_header_check(23)
        list(APPEND JPLCZ_MICROFMT_HEADER_CHECK_TARGETS jplcz_microfmt_headers_cxx23)
        set(JPLCZ_MICROFMT_HAS_CXX23_WRAPPER TRUE)
    endif()

    add_custom_target(jplcz_microfmt_check_public_headers
        DEPENDS ${JPLCZ_MICROFMT_HEADER_CHECK_TARGETS}
    )
endif()

if(JPLCZ_MICROFMT_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.zip
    )
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.zip
    )

    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest nlohmann_json)

    enable_testing()

    add_executable(jplcz_microfmt_tests tests/test_core.cpp
        tests/test_scratch_allocator.cpp
        tests/test_metadata_map.cpp
        tests/test_task_thread.cpp
        tests/test_gdb_protocol.cpp
        tests/test_gdb_registers.cpp
        tests/test_dwarf_registers.cpp
        tests/test_register_context_with.cpp
        tests/test_remote_layout_accessor.cpp
        tests/test_remote_basic_string.cpp
        tests/test_remote_page_table_walker.cpp
        tests/test_memory_pattern_scanners.cpp
        tests/test_gdb_register_array.cpp
        tests/test_register_xml_printer.cpp
        tests/test_inspector.cpp
        tests/test_bitfield.cpp
        tests/test_can.cpp
        tests/test_cbor.cpp
        tests/test_fixed_point.cpp
        tests/test_ranges.cpp
        tests/test_spi.cpp
        tests/test_escaped.cpp
        tests/test_filter_view.cpp
        tests/test_repeated_view.cpp
        tests/test_memory_diff.cpp
        tests/test_remote_memory_diff.cpp
        tests/test_additional_formatters.cpp
        tests/test_fmt_compat.cpp
        tests/test_i2c.cpp
        tests/test_json.cpp
        tests/test_binary.cpp
        tests/test_uuid.cpp
        tests/test_semver.cpp
        tests/test_units.cpp
        tests/test_chrono.cpp
        tests/test_ring_buffer.cpp
        tests/test_base_views.cpp
        tests/test_math.cpp
        tests/test_map_view.cpp
        tests/test_monad.cpp
        tests/test_source_location.cpp
        tests/test_styled.cpp
        tests/test_styled_sink.cpp
        tests/test_tee_sink.cpp
        tests/test_tuple.cpp
        tests/test_container_sink.cpp
        tests/test_pmr_sink.cpp
        tests/test_platform_log_sinks.cpp
        tests/test_pointer.cpp
        tests/test_logger_override.cpp
        tests/test_tls_provider.cpp
        tests/test_sbuf_sink.cpp)
    target_link_libraries(jplcz_microfmt_tests PRIVATE jplcz_microfmt GTest::gtest_main
        nlohmann_json::nlohmann_json)
    target_compile_features(jplcz_microfmt_tests PRIVATE cxx_std_20)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_tests)
    if(NOT MSVC)
        target_sources(jplcz_microfmt_tests PRIVATE
            tests/test_probes.cpp
            tests/test_stdio.cpp)
    endif()
    if(JPLCZ_MICROFMT_HAS_SYSLOG)
        target_compile_definitions(jplcz_microfmt_tests PRIVATE
            MICROFMT_COMPILE_WITH_SYSLOG=1)
    endif()

    include(GoogleTest)
    gtest_discover_tests(jplcz_microfmt_tests)

    if(TARGET PkgConfig::JPLCZ_MICROFMT_SYSTEMD)
        target_sources(jplcz_microfmt_tests PRIVATE tests/test_systemd_sink.cpp)
        target_link_libraries(jplcz_microfmt_tests PRIVATE PkgConfig::JPLCZ_MICROFMT_SYSTEMD)
    endif()

    if(TARGET PkgConfig::JPLCZ_MICROFMT_TIZEN_DLOG)
        target_sources(jplcz_microfmt_tests PRIVATE tests/test_tizen_dlog_sink.cpp)
        target_link_libraries(jplcz_microfmt_tests PRIVATE
            PkgConfig::JPLCZ_MICROFMT_TIZEN_DLOG)
    endif()

    if(Boost_FOUND)
        target_sources(jplcz_microfmt_tests PRIVATE
            tests/test_boost_formatters.cpp
            tests/test_map_view_boost.cpp)
    endif()

    add_executable(jplcz_microfmt_cxx17_container_sink_tests
        tests/test_container_sink.cpp)
    target_link_libraries(jplcz_microfmt_cxx17_container_sink_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_container_sink_tests)
    set_target_properties(jplcz_microfmt_cxx17_container_sink_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_container_sink_tests
        TEST_PREFIX "Cxx17ContainerSink.")

    add_executable(jplcz_microfmt_cxx17_remote_basic_string_tests
        tests/test_remote_basic_string.cpp)
    target_link_libraries(jplcz_microfmt_cxx17_remote_basic_string_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_remote_basic_string_tests)
    set_target_properties(
        jplcz_microfmt_cxx17_remote_basic_string_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_remote_basic_string_tests
        TEST_PREFIX "Cxx17RemoteBasicString.")

    add_executable(jplcz_microfmt_cxx17_remote_layout_accessor_tests
        tests/test_remote_layout_accessor.cpp)
    target_link_libraries(
        jplcz_microfmt_cxx17_remote_layout_accessor_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_remote_layout_accessor_tests)
    set_target_properties(
        jplcz_microfmt_cxx17_remote_layout_accessor_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_remote_layout_accessor_tests
        TEST_PREFIX "Cxx17RemoteLayoutAccessor.")

    add_executable(jplcz_microfmt_cxx17_remote_page_table_walker_tests
        tests/test_remote_page_table_walker.cpp)
    target_link_libraries(
        jplcz_microfmt_cxx17_remote_page_table_walker_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(
        jplcz_microfmt_cxx17_remote_page_table_walker_tests)
    set_target_properties(
        jplcz_microfmt_cxx17_remote_page_table_walker_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(
        jplcz_microfmt_cxx17_remote_page_table_walker_tests
        TEST_PREFIX "Cxx17RemotePageTableWalker.")

    add_executable(jplcz_microfmt_cxx17_register_context_with_tests
        tests/test_register_context_with.cpp)
    target_link_libraries(
        jplcz_microfmt_cxx17_register_context_with_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_register_context_with_tests)
    set_target_properties(
        jplcz_microfmt_cxx17_register_context_with_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_register_context_with_tests
        TEST_PREFIX "Cxx17RegisterContextWith.")

    add_executable(jplcz_microfmt_cxx17_scratch_allocator_tests
        tests/test_scratch_allocator.cpp)
    target_link_libraries(jplcz_microfmt_cxx17_scratch_allocator_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_scratch_allocator_tests)
    set_target_properties(jplcz_microfmt_cxx17_scratch_allocator_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_scratch_allocator_tests
        TEST_PREFIX "Cxx17ScratchAllocator.")

    add_executable(jplcz_microfmt_cxx17_tls_provider_tests
        tests/test_tls_provider.cpp)
    target_link_libraries(jplcz_microfmt_cxx17_tls_provider_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_tls_provider_tests)
    set_target_properties(jplcz_microfmt_cxx17_tls_provider_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_tls_provider_tests
        TEST_PREFIX "Cxx17TlsProvider.")

    add_executable(jplcz_microfmt_cxx17_metadata_map_tests
        tests/test_metadata_map.cpp)
    target_link_libraries(jplcz_microfmt_cxx17_metadata_map_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_metadata_map_tests)
    set_target_properties(jplcz_microfmt_cxx17_metadata_map_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_metadata_map_tests
        TEST_PREFIX "Cxx17MetadataMap.")

    add_executable(jplcz_microfmt_cxx17_task_thread_tests
        tests/test_task_thread.cpp)
    target_link_libraries(jplcz_microfmt_cxx17_task_thread_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_task_thread_tests)
    set_target_properties(jplcz_microfmt_cxx17_task_thread_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_task_thread_tests
        TEST_PREFIX "Cxx17TaskThread.")

    add_executable(jplcz_microfmt_cxx17_gdb_protocol_tests
        tests/test_gdb_protocol.cpp)
    target_link_libraries(jplcz_microfmt_cxx17_gdb_protocol_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_gdb_protocol_tests)
    set_target_properties(jplcz_microfmt_cxx17_gdb_protocol_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_gdb_protocol_tests
        TEST_PREFIX "Cxx17GdbProtocol.")

    add_executable(jplcz_microfmt_cxx17_gdb_register_tests
        tests/test_gdb_registers.cpp)
    target_link_libraries(jplcz_microfmt_cxx17_gdb_register_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_gdb_register_tests)
    set_target_properties(jplcz_microfmt_cxx17_gdb_register_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_gdb_register_tests
        TEST_PREFIX "Cxx17GdbRegisters.")

    add_executable(jplcz_microfmt_cxx17_gdb_register_array_tests
        tests/test_gdb_register_array.cpp)
    target_link_libraries(jplcz_microfmt_cxx17_gdb_register_array_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_gdb_register_array_tests)
    set_target_properties(
        jplcz_microfmt_cxx17_gdb_register_array_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_gdb_register_array_tests
        TEST_PREFIX "Cxx17GdbRegisterArray.")

    add_executable(jplcz_microfmt_cxx17_register_xml_printer_tests
        tests/test_register_xml_printer.cpp)
    target_link_libraries(jplcz_microfmt_cxx17_register_xml_printer_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_register_xml_printer_tests)
    set_target_properties(
        jplcz_microfmt_cxx17_register_xml_printer_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_register_xml_printer_tests
        TEST_PREFIX "Cxx17RegisterXmlPrinter.")

    add_executable(jplcz_microfmt_cxx17_pmr_sink_tests tests/test_pmr_sink.cpp)
    target_link_libraries(jplcz_microfmt_cxx17_pmr_sink_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_pmr_sink_tests)
    set_target_properties(jplcz_microfmt_cxx17_pmr_sink_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_pmr_sink_tests
        TEST_PREFIX "Cxx17PmrSink.")

    add_executable(jplcz_microfmt_cxx17_json_tests tests/test_json.cpp)
    target_link_libraries(jplcz_microfmt_cxx17_json_tests PRIVATE
        jplcz_microfmt GTest::gtest_main nlohmann_json::nlohmann_json)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_json_tests)
    set_target_properties(jplcz_microfmt_cxx17_json_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_json_tests
        TEST_PREFIX "Cxx17Json.")

    add_executable(jplcz_microfmt_cxx17_cbor_tests tests/test_cbor.cpp)
    target_link_libraries(jplcz_microfmt_cxx17_cbor_tests PRIVATE
        jplcz_microfmt GTest::gtest_main nlohmann_json::nlohmann_json)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_cbor_tests)
    set_target_properties(jplcz_microfmt_cxx17_cbor_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_cbor_tests
        TEST_PREFIX "Cxx17Cbor.")

    add_executable(jplcz_microfmt_cxx17_tuple_tests tests/test_tuple.cpp)
    target_link_libraries(jplcz_microfmt_cxx17_tuple_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_tuple_tests)
    set_target_properties(jplcz_microfmt_cxx17_tuple_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_tuple_tests
        TEST_PREFIX "Cxx17Tuple.")

    add_executable(jplcz_microfmt_cxx17_map_view_tests tests/test_map_view.cpp)
    target_link_libraries(jplcz_microfmt_cxx17_map_view_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_map_view_tests)
    set_target_properties(jplcz_microfmt_cxx17_map_view_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_map_view_tests
        TEST_PREFIX "Cxx17MapView.")

    add_executable(jplcz_microfmt_cxx17_pointer_tests tests/test_pointer.cpp)
    target_link_libraries(jplcz_microfmt_cxx17_pointer_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_pointer_tests)
    set_target_properties(jplcz_microfmt_cxx17_pointer_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_pointer_tests
        TEST_PREFIX "Cxx17Pointer.")

    add_executable(jplcz_microfmt_cxx17_filter_view_tests
        tests/test_filter_view.cpp)
    target_link_libraries(jplcz_microfmt_cxx17_filter_view_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_filter_view_tests)
    set_target_properties(jplcz_microfmt_cxx17_filter_view_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_filter_view_tests
        TEST_PREFIX "Cxx17FilterView.")

    add_executable(jplcz_microfmt_cxx17_repeated_view_tests
        tests/test_repeated_view.cpp)
    target_link_libraries(jplcz_microfmt_cxx17_repeated_view_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_repeated_view_tests)
    set_target_properties(jplcz_microfmt_cxx17_repeated_view_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_repeated_view_tests
        TEST_PREFIX "Cxx17RepeatedView.")

    add_executable(jplcz_microfmt_cxx17_memory_diff_tests
        tests/test_memory_diff.cpp)
    target_link_libraries(jplcz_microfmt_cxx17_memory_diff_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_memory_diff_tests)
    set_target_properties(jplcz_microfmt_cxx17_memory_diff_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_memory_diff_tests
        TEST_PREFIX "Cxx17MemoryDiff.")

    add_executable(jplcz_microfmt_cxx17_remote_memory_diff_tests
        tests/test_remote_memory_diff.cpp)
    target_link_libraries(jplcz_microfmt_cxx17_remote_memory_diff_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_remote_memory_diff_tests)
    set_target_properties(jplcz_microfmt_cxx17_remote_memory_diff_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    gtest_discover_tests(jplcz_microfmt_cxx17_remote_memory_diff_tests
        TEST_PREFIX "Cxx17RemoteMemoryDiff.")

    # microfmt/inspector/dl_elf_enumerator.hpp is dlfcn.h/dl_iterate_phdr
    # based and only supports Linux and BSD systems (see the #error guard at
    # the top of the header).
    if(UNIX AND NOT APPLE)
        add_executable(jplcz_microfmt_cxx17_dl_elf_enumerator_tests
            tests/test_dl_elf_enumerator.cpp)
        target_link_libraries(jplcz_microfmt_cxx17_dl_elf_enumerator_tests PRIVATE
            jplcz_microfmt GTest::gtest_main ${CMAKE_DL_LIBS})
        jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx17_dl_elf_enumerator_tests)
        set_target_properties(jplcz_microfmt_cxx17_dl_elf_enumerator_tests PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
        )
        gtest_discover_tests(jplcz_microfmt_cxx17_dl_elf_enumerator_tests
            TEST_PREFIX "Cxx17DlElfEnumerator.")
    endif()

    if(JPLCZ_MICROFMT_COMPILER_SUPPORTS_CXX23)
        add_executable(jplcz_microfmt_cxx23_tests tests/test_monad.cpp)
        target_link_libraries(jplcz_microfmt_cxx23_tests PRIVATE
            jplcz_microfmt GTest::gtest_main)
        jplcz_microfmt_enable_warnings(jplcz_microfmt_cxx23_tests)
        set_target_properties(jplcz_microfmt_cxx23_tests PROPERTIES
            CXX_STANDARD 23
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
        )
        gtest_discover_tests(jplcz_microfmt_cxx23_tests)
    endif()

    if(NOT MSVC)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)

        # Build probe object file with optimization
        add_library(jplcz_microfmt_stack_probes OBJECT
            tests/stack_probes.cpp)
        target_link_libraries(jplcz_microfmt_stack_probes PRIVATE jplcz_microfmt)
        target_compile_features(jplcz_microfmt_stack_probes PRIVATE cxx_std_20)
        jplcz_microfmt_enable_warnings(jplcz_microfmt_stack_probes)
    endif()

    if(NOT MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(jplcz_microfmt_stack_probes PRIVATE
            -O2
            -fstack-usage
        )

        add_custom_target(check_stack_usage
            COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_stack_usage.py
            --compiler gcc
            --input-dir $<TARGET_PROPERTY:jplcz_microfmt_stack_probes,BINARY_DIR>
            --budget 256
            --warn-budget 192
            DEPENDS jplcz_microfmt_stack_probes
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Auditing GCC stack frame allocations"
            VERBATIM
        )

    elseif(NOT MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(jplcz_microfmt_stack_probes PRIVATE
            -O2
            -fstack-size-section
        )

        find_program(LLVM_READELF NAMES llvm-readelf readelf)

        if(LLVM_READELF)
            add_custom_target(check_stack_usage
                COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_stack_usage.py
                --compiler clang
                --input $<TARGET_OBJECTS:jplcz_microfmt_stack_probes>
                --readelf ${LLVM_READELF}
                --budget 256
                --warn-budget 192
                DEPENDS jplcz_microfmt_stack_probes
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                COMMENT "Auditing Clang stack frame allocations"
                VERBATIM
            )
        endif()
    endif()
endif()

if(JPLCZ_MICROFMT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(JPLCZ_MICROFMT_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()

if(JPLCZ_MICROFMT_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(
        TARGETS jplcz_microfmt
        EXPORT jplcz_microfmtTargets
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )

    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        COMPONENT Development
    )

    # Plain-text/Markdown documentation (no Doxygen/HTML build step
    # required). `docs/` is installed as a subdirectory, matching the
    # source-tree layout, so README.md's relative `docs/...` links and the
    # cross-links between guides under docs/ keep resolving after install.
    install(
        FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}"
        COMPONENT Documentation
    )

    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs/"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs"
        COMPONENT Documentation
        PATTERN "*.css" EXCLUDE
    )

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/jplcz_microfmtConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/jplcz_microfmtConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_DATADIR}/cmake/jplcz_microfmt"
        NO_SET_AND_CHECK_MACRO
        NO_CHECK_REQUIRED_COMPONENTS_MACRO
    )

    # Header-only INTERFACE library: the package config is architecture
    # independent (no compiled binaries to match ABI/bitness against), so
    # skip the platform check and install under share/, not lib/.
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/jplcz_microfmtConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT
    )

    install(
        EXPORT jplcz_microfmtTargets
        FILE jplcz_microfmtTargets.cmake
        NAMESPACE jplcz_microfmt::
        DESTINATION "${CMAKE_INSTALL_DATADIR}/cmake/jplcz_microfmt"
        COMPONENT Development
    )

    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/jplcz_microfmtConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/jplcz_microfmtConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/cmake/jplcz_microfmt"
        COMPONENT Development
    )
endif()

if(JPLCZ_MICROFMT_BUILD_MANPAGES)
    # man(7) pages generated from the same Markdown docs installed above, via
    # pandoc (no Doxygen/HTML involved). Opt-in: most consumers never need
    # man pages, and not every platform ships pandoc, so this only runs when
    # explicitly requested.
    find_program(JPLCZ_MICROFMT_PANDOC_EXECUTABLE pandoc)
    if(NOT JPLCZ_MICROFMT_PANDOC_EXECUTABLE)
        message(FATAL_ERROR
            "JPLCZ_MICROFMT_BUILD_MANPAGES=ON requires pandoc, which was not "
            "found on PATH. Install pandoc or turn this option off.")
    endif()

    include(GNUInstallDirs) # idempotent if already included above

    set(JPLCZ_MICROFMT_MAN_SECTION 7)
    set(JPLCZ_MICROFMT_MAN_HEADER "jplcz_microfmt Manual")
    set(JPLCZ_MICROFMT_MAN_FOOTER "jplcz_microfmt ${PROJECT_VERSION}")
    set(JPLCZ_MICROFMT_MAN_OUTPUT_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/man/man${JPLCZ_MICROFMT_MAN_SECTION}")
    file(MAKE_DIRECTORY "${JPLCZ_MICROFMT_MAN_OUTPUT_DIR}")

    file(GLOB JPLCZ_MICROFMT_MAN_DOC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/docs/*.md")
    list(SORT JPLCZ_MICROFMT_MAN_DOC_SOURCES)

    # README.md becomes the top-level jplcz_microfmt(7) overview page; every
    # top-level docs/*.md guide becomes jplcz_microfmt-<name>(7). Guides
    # nested under docs/formatters/ and docs/inspector/ are deep-dive
    # references linked from the top-level guides and are not converted
    # individually.
    set(JPLCZ_MICROFMT_MAN_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md" ${JPLCZ_MICROFMT_MAN_DOC_SOURCES})
    set(JPLCZ_MICROFMT_MAN_OUTPUTS "")

    foreach(doc IN LISTS JPLCZ_MICROFMT_MAN_SOURCES)
        get_filename_component(JPLCZ_MICROFMT_MAN_NAME "${doc}" NAME_WE)
        if(JPLCZ_MICROFMT_MAN_NAME STREQUAL "README")
            set(JPLCZ_MICROFMT_MAN_PAGE_NAME "jplcz_microfmt")
        else()
            set(JPLCZ_MICROFMT_MAN_PAGE_NAME "jplcz_microfmt-${JPLCZ_MICROFMT_MAN_NAME}")
        endif()
        set(JPLCZ_MICROFMT_MAN_OUTPUT
            "${JPLCZ_MICROFMT_MAN_OUTPUT_DIR}/${JPLCZ_MICROFMT_MAN_PAGE_NAME}.${JPLCZ_MICROFMT_MAN_SECTION}")
        add_custom_command(
            OUTPUT "${JPLCZ_MICROFMT_MAN_OUTPUT}"
            COMMAND "${JPLCZ_MICROFMT_PANDOC_EXECUTABLE}"
                    -s -f gfm -t man
                    "-Mtitle=${JPLCZ_MICROFMT_MAN_PAGE_NAME}"
                    "-Msection=${JPLCZ_MICROFMT_MAN_SECTION}"
                    "-Mheader=${JPLCZ_MICROFMT_MAN_HEADER}"
                    "-Mfooter=${JPLCZ_MICROFMT_MAN_FOOTER}"
                    "${doc}"
                    -o "${JPLCZ_MICROFMT_MAN_OUTPUT}"
            DEPENDS "${doc}"
            COMMENT "pandoc: ${JPLCZ_MICROFMT_MAN_PAGE_NAME}.${JPLCZ_MICROFMT_MAN_SECTION}"
            VERBATIM
        )
        list(APPEND JPLCZ_MICROFMT_MAN_OUTPUTS "${JPLCZ_MICROFMT_MAN_OUTPUT}")
    endforeach()

    add_custom_target(jplcz_microfmt_manpages ALL DEPENDS ${JPLCZ_MICROFMT_MAN_OUTPUTS})

    if(JPLCZ_MICROFMT_INSTALL)
        install(
            FILES ${JPLCZ_MICROFMT_MAN_OUTPUTS}
            DESTINATION "${CMAKE_INSTALL_MANDIR}/man${JPLCZ_MICROFMT_MAN_SECTION}"
            COMPONENT Documentation
        )
    endif()
endif()

if(JPLCZ_MICROFMT_IS_TOP_LEVEL AND JPLCZ_MICROFMT_INSTALL)
    set(CPACK_PACKAGE_NAME "jplcz-microfmt")
    set(CPACK_PACKAGE_VENDOR "Jaroslaw Pelczar")
    set(CPACK_PACKAGE_CONTACT "Jaroslaw Pelczar <jarek@jpelczar.com>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "Header-only allocation-free formatting and remote inspection library")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/jplcz/microfmt")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
    set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
    set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "jplcz_microfmt")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    set(CPACK_VERBATIM_VARIABLES ON)
    set(CPACK_THREADS 0)

    set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")
    set(CPACK_SOURCE_IGNORE_FILES
        "/\\.git/"
        "/\\.idea/"
        "/\\.vscode/"
        "/build[^/]*/"
        "/cmake-build-[^/]*/"
        "/_deps/"
    )

    # Header-only package: archives always work everywhere; DEB/RPM are added
    # only when their packaging tools are available on the host.
    set(CPACK_GENERATOR "TGZ;ZIP")

    find_program(JPLCZ_MICROFMT_DPKG_DEB_EXECUTABLE dpkg-deb)
    if(JPLCZ_MICROFMT_DPKG_DEB_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "DEB")
    endif()

    find_program(JPLCZ_MICROFMT_RPMBUILD_EXECUTABLE rpmbuild)
    if(JPLCZ_MICROFMT_RPMBUILD_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "RPM")
    endif()

    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "libdevel")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "all")
    set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")

    set(CPACK_RPM_PACKAGE_LICENSE "BSD-2-Clause")
    set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "noarch")
    set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
    set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")

    include(CPack)
endif()