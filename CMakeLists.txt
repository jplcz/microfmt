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

# Optional test runner
option(MICROFMT_BUILD_TESTS "Build test suite" ON)
if(MICROFMT_BUILD_TESTS)
    add_executable(microfmt_tests tests/main.cpp)
    target_link_libraries(microfmt_tests PRIVATE microfmt)
    target_compile_options(microfmt_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
            -fno-exceptions -fno-rtti
        >
    )
    enable_testing()
    add_test(NAME unit_tests COMMAND microfmt_tests)
endif()