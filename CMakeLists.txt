cmake_minimum_required(VERSION 3.20)
project(microfmt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
        tests/test_ring_buffer.cpp)
    target_link_libraries(microfmt_tests PRIVATE microfmt GTest::gtest_main)

    target_compile_options(microfmt_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
        >
    )

    include(GoogleTest)
    gtest_discover_tests(microfmt_tests)

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
