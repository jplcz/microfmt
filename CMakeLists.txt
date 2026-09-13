cmake_minimum_required(VERSION 3.20)
project(microfmt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckCXXCompilerFlag)

find_package(Boost 1.77 CONFIG QUIET)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(MICROFMT_SYSTEMD QUIET IMPORTED_TARGET libsystemd)
    pkg_check_modules(MICROFMT_TIZEN_DLOG QUIET IMPORTED_TARGET dlog)
endif()

if(MSVC)
    check_cxx_compiler_flag("/std:c++latest" MICROFMT_COMPILER_SUPPORTS_CXX23)
else()
    check_cxx_compiler_flag("-std=c++23" MICROFMT_COMPILER_SUPPORTS_CXX23)
endif()

# Header-only interface target
add_library(microfmt INTERFACE)

target_include_directories(microfmt INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

add_library(microfmt::microfmt ALIAS microfmt)

# Optional test runner
option(MICROFMT_BUILD_TESTS "Build test suite" ON)
option(MICROFMT_BUILD_EXAMPLES "Build example apps" ON)
option(MICROFMT_BUILD_HEADER_CHECKS
    "Compile all public headers under each supported C++ standard" ON)

if(MICROFMT_BUILD_HEADER_CHECKS)
    function(add_microfmt_header_check STANDARD)
        add_library(microfmt_headers_cxx${STANDARD} OBJECT
            tests/compile_all_headers.cpp)
        target_link_libraries(microfmt_headers_cxx${STANDARD} PRIVATE microfmt)
        set_target_properties(microfmt_headers_cxx${STANDARD} PROPERTIES
            CXX_STANDARD ${STANDARD}
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
        )
        target_compile_definitions(microfmt_headers_cxx${STANDARD} PRIVATE
            MICROFMT_HEADER_CHECK_STANDARD=${STANDARD}
        )

        if(Boost_FOUND)
            target_link_libraries(microfmt_headers_cxx${STANDARD} PRIVATE
                Boost::boost)
            target_compile_definitions(microfmt_headers_cxx${STANDARD} PRIVATE
                MICROFMT_COMPILE_WITH_BOOST_DESCRIBE=1
                MICROFMT_ENABLE_BOOST_UUID=1
                MICROFMT_ENABLE_BOOST_SOURCE_LOCATION=1)
        endif()

        if(STANDARD GREATER_EQUAL 20 AND TARGET PkgConfig::MICROFMT_SYSTEMD)
            target_link_libraries(microfmt_headers_cxx${STANDARD} PRIVATE
                PkgConfig::MICROFMT_SYSTEMD)
            target_compile_definitions(microfmt_headers_cxx${STANDARD} PRIVATE
                MICROFMT_COMPILE_WITH_SYSTEMD=1)
        endif()

        if(STANDARD GREATER_EQUAL 20 AND TARGET PkgConfig::MICROFMT_TIZEN_DLOG)
            target_link_libraries(microfmt_headers_cxx${STANDARD} PRIVATE
                PkgConfig::MICROFMT_TIZEN_DLOG)
            target_compile_definitions(microfmt_headers_cxx${STANDARD} PRIVATE
                MICROFMT_COMPILE_WITH_TIZEN_DLOG=1)
        endif()
    endfunction()

    add_microfmt_header_check(17)
    add_microfmt_header_check(20)

    set(MICROFMT_HEADER_CHECK_TARGETS
        microfmt_headers_cxx17
        microfmt_headers_cxx20
    )
    set(MICROFMT_HAS_CXX23_WRAPPER FALSE)
    if(MICROFMT_COMPILER_SUPPORTS_CXX23)
        add_microfmt_header_check(23)
        list(APPEND MICROFMT_HEADER_CHECK_TARGETS microfmt_headers_cxx23)
        set(MICROFMT_HAS_CXX23_WRAPPER TRUE)
    endif()

    add_custom_target(check_public_headers
        DEPENDS ${MICROFMT_HEADER_CHECK_TARGETS}
    )
endif()

if(MICROFMT_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )

    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    add_executable(microfmt_tests tests/test_core.cpp
        tests/test_bitfield.cpp
        tests/test_fixed_point.cpp
        tests/test_ranges.cpp
        tests/test_escaped.cpp
        tests/test_probes.cpp
        tests/test_fmt_compat.cpp
        tests/test_stdio.cpp
        tests/test_binary.cpp
        tests/test_uuid.cpp
        tests/test_semver.cpp
        tests/test_units.cpp
        tests/test_chrono.cpp
        tests/test_ring_buffer.cpp
        tests/test_base_views.cpp
        tests/test_math.cpp
        tests/test_monad.cpp
        tests/test_source_location.cpp
        tests/test_tee_sink.cpp
        tests/test_container_sink.cpp
        tests/test_pmr_sink.cpp
        tests/test_platform_log_sinks.cpp
        tests/test_logger_override.cpp)
    target_link_libraries(microfmt_tests PRIVATE microfmt GTest::gtest_main)

    target_compile_options(microfmt_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
        >
    )

    include(GoogleTest)
    gtest_discover_tests(microfmt_tests)

    if(TARGET PkgConfig::MICROFMT_SYSTEMD)
        target_sources(microfmt_tests PRIVATE tests/test_systemd_sink.cpp)
        target_link_libraries(microfmt_tests PRIVATE PkgConfig::MICROFMT_SYSTEMD)
    endif()

    if(TARGET PkgConfig::MICROFMT_TIZEN_DLOG)
        target_sources(microfmt_tests PRIVATE tests/test_tizen_dlog_sink.cpp)
        target_link_libraries(microfmt_tests PRIVATE
            PkgConfig::MICROFMT_TIZEN_DLOG)
    endif()

    add_executable(microfmt_cxx17_container_sink_tests
        tests/test_container_sink.cpp)
    target_link_libraries(microfmt_cxx17_container_sink_tests PRIVATE
        microfmt GTest::gtest_main)
    set_target_properties(microfmt_cxx17_container_sink_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_options(microfmt_cxx17_container_sink_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
        >
    )
    gtest_discover_tests(microfmt_cxx17_container_sink_tests
        TEST_PREFIX "Cxx17ContainerSink.")

    add_executable(microfmt_cxx17_pmr_sink_tests tests/test_pmr_sink.cpp)
    target_link_libraries(microfmt_cxx17_pmr_sink_tests PRIVATE
        microfmt GTest::gtest_main)
    set_target_properties(microfmt_cxx17_pmr_sink_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_options(microfmt_cxx17_pmr_sink_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
        >
    )
    gtest_discover_tests(microfmt_cxx17_pmr_sink_tests
        TEST_PREFIX "Cxx17PmrSink.")

    if(MICROFMT_COMPILER_SUPPORTS_CXX23)
        add_executable(microfmt_cxx23_tests tests/test_monad.cpp)
        target_link_libraries(microfmt_cxx23_tests PRIVATE
            microfmt GTest::gtest_main)
        set_target_properties(microfmt_cxx23_tests PROPERTIES
            CXX_STANDARD 23
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
        )
        target_compile_options(microfmt_cxx23_tests PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
            >
        )
        gtest_discover_tests(microfmt_cxx23_tests)
    endif()

    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    # Build probe object file with optimization
    add_library(microfmt_stack_probes OBJECT
        tests/stack_probes.cpp)
    target_link_libraries(microfmt_stack_probes PRIVATE microfmt)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(microfmt_stack_probes PRIVATE
            -O2
            -fstack-usage
        )

        add_custom_target(check_stack_usage
            COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_stack_usage.py
            --compiler gcc
            --input $<TARGET_OBJECTS:microfmt_stack_probes>.su
            --budget 256
            --warn-budget 192
            DEPENDS microfmt_stack_probes
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Auditing GCC stack frame allocations"
            VERBATIM
        )

    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(microfmt_stack_probes PRIVATE
            -O2
            -fstack-size-section
        )

        find_program(LLVM_READELF NAMES llvm-readelf readelf)

        if(LLVM_READELF)
            add_custom_target(check_stack_usage
                COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_stack_usage.py
                --compiler clang
                --input $<TARGET_OBJECTS:microfmt_stack_probes>
                --readelf ${LLVM_READELF}
                --budget 256
                --warn-budget 192
                DEPENDS microfmt_stack_probes
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                COMMENT "Auditing Clang stack frame allocations"
                VERBATIM
            )
        endif()
    endif()
endif()

if(MICROFMT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
