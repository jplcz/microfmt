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

set(JPLCZ_MICROFMT_PORTING_HEADERS "" CACHE PATH
    "Directory containing target-specific override headers (currently \
default_log_sink.hpp) copied into include/microfmt/detail/porting/ and \
installed alongside microfmt's own headers, plus the matching \
MICROFMT_*_BACKEND_CUSTOM macro defined automatically for consumers of \
the jplcz_microfmt target. See include/microfmt/detail/porting/\
*.template.hpp and include/microfmt/detail/porting/README.md for the \
exact shape each file must have.")

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
# jplcz_reloco dependency
#
# jplcz_reloco provides the hardened container / lifetime-safety primitives
# that jplcz_microfmt is migrating onto. Callers that already vendor or pin a
# specific reloco checkout can point JPLCZ_MICROFMT_RELOCO_SOURCE_DIR at it;
# otherwise it is fetched from its GitHub repository at a pinned commit.
#
# If a parent project already brought in jplcz_reloco itself (e.g. its own
# `add_subdirectory(path/to/reloco)` before `add_subdirectory(path/to/microfmt)`,
# or its own earlier FetchContent_MakeAvailable(jplcz_reloco)), the
# jplcz_reloco::reloco target already exists by the time this file runs:
# skip fetching/declaring it a second time and reuse the parent's target
# as-is (including whatever options the parent already configured it
# with) instead of silently overriding it.
if(NOT TARGET jplcz_reloco::reloco)
    set(JPLCZ_MICROFMT_RELOCO_SOURCE_DIR "" CACHE PATH
        "Path to a local jplcz_reloco checkout to use instead of fetching it")
    # Allow the local checkout path to be supplied via an environment variable
    # (e.g. for CI or shell profiles) without having to pass -D on every configure.
    # The explicit CMake cache variable/-D option always takes precedence.
    if(NOT JPLCZ_MICROFMT_RELOCO_SOURCE_DIR AND DEFINED ENV{JPLCZ_MICROFMT_RELOCO_SOURCE_DIR})
        set(JPLCZ_MICROFMT_RELOCO_SOURCE_DIR "$ENV{JPLCZ_MICROFMT_RELOCO_SOURCE_DIR}" CACHE PATH
            "Path to a local jplcz_reloco checkout to use instead of fetching it" FORCE)
    endif()
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
endif()

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

# JPLCZ_MICROFMT_PORTING_HEADERS: copy whichever of default_log_sink.hpp
# (currently the only entry) exist in the given directory into a staging
# tree (never into microfmt's own source include/ tree, so microfmt's own
# headers are never touched/replaced), and bake the matching
# MICROFMT_*_BACKEND_CUSTOM macro into a generated header (see
# cmake/generated_porting_config.hpp.in) placed in that same staging tree
# -- rather than an INTERFACE target_compile_definitions, so the override
# takes effect for every consumer of the plain include/ tree, including
# one that never links jplcz_microfmt as an actual CMake target (e.g. a
# hand-copied include directory, or a downstream
# find_package(jplcz_microfmt) consumer using a different build system
# than the one that produced the install tree). See
# include/microfmt/detail/porting/README.md for the full mechanism, and
# reloco's own JPLCZ_RELOCO_PORTING_HEADERS block (reloco/CMakeLists.txt)
# for the layout this mirrors.
#
# The build-tree staging layout is "porting_include/detail/porting/*.hpp"
# (no "microfmt/" prefix) plus an empty "porting_include/log/" directory,
# with "porting_include/log" (not "porting_include" itself) added as the
# extra -I directory: each microfmt header living one level deeper than
# reloco's (log/logger.hpp, vs. reloco's own mutex.hpp directly under
# reloco/) spells its fixed include as #include
# "../detail/porting/<name>.hpp" (with a leading "../"), a quoted
# include, so the compiler first tries it relative to that header's own
# directory, finds nothing there (only the *.template.hpp scaffolds live
# in the real source tree), then falls back to treating it like #include
# <log/../detail/porting/<name>.hpp> against every -I directory. That
# literal "log/../" segment needs an -I directory one level *above*
# detail/porting/ (i.e. porting_include/log, not porting_include) for the
# "../" to land back on porting_include/detail/porting/ -- and the
# intermediate "log" path component must exist as a real directory (even
# though nothing is ever placed inside it) for the OS to resolve the ".."
# that follows it. The installed layout is different (see the
# JPLCZ_MICROFMT_INSTALL block below): there, the same quoted include
# resolves directly against <prefix>/include/microfmt/detail/porting/
# (install-tree-relative to logger.hpp itself, one real directory level
# above it), so the installed copy is placed at that "microfmt/"-prefixed
# path instead, matching the *.template.hpp scaffolds already installed
# there.
if(JPLCZ_MICROFMT_PORTING_HEADERS)
    set(JPLCZ_MICROFMT_PORTING_STAGING_DIR "${CMAKE_CURRENT_BINARY_DIR}/porting_include")
    set(JPLCZ_MICROFMT_PORTING_DEST_DIR "${JPLCZ_MICROFMT_PORTING_STAGING_DIR}/detail/porting")
    file(MAKE_DIRECTORY "${JPLCZ_MICROFMT_PORTING_DEST_DIR}")
    # See the "../" note above: this directory is never populated, it
    # only needs to exist so "${JPLCZ_MICROFMT_PORTING_STAGING_DIR}/log/.."
    # resolves back to "${JPLCZ_MICROFMT_PORTING_STAGING_DIR}".
    file(MAKE_DIRECTORY "${JPLCZ_MICROFMT_PORTING_STAGING_DIR}/log")

    set(JPLCZ_MICROFMT_PORTING_HEADER_NAMES default_log_sink.hpp)
    # CMake variable consulted by cmake/generated_porting_config.hpp.in's
    # matching #cmakedefine, one per header name above, same order.
    set(JPLCZ_MICROFMT_PORTING_HEADER_VARS
        MICROFMT_DEFAULT_LOG_SINK_BACKEND_CUSTOM
    )

    list(LENGTH JPLCZ_MICROFMT_PORTING_HEADER_NAMES JPLCZ_MICROFMT_PORTING_HEADER_COUNT)
    math(EXPR JPLCZ_MICROFMT_PORTING_HEADER_LAST "${JPLCZ_MICROFMT_PORTING_HEADER_COUNT} - 1")

    foreach(JPLCZ_MICROFMT_PORTING_I RANGE ${JPLCZ_MICROFMT_PORTING_HEADER_LAST})
        list(GET JPLCZ_MICROFMT_PORTING_HEADER_NAMES ${JPLCZ_MICROFMT_PORTING_I} JPLCZ_MICROFMT_PORTING_HEADER_NAME)
        list(GET JPLCZ_MICROFMT_PORTING_HEADER_VARS ${JPLCZ_MICROFMT_PORTING_I} JPLCZ_MICROFMT_PORTING_HEADER_VAR)
        set(JPLCZ_MICROFMT_PORTING_SRC "${JPLCZ_MICROFMT_PORTING_HEADERS}/${JPLCZ_MICROFMT_PORTING_HEADER_NAME}")
        if(EXISTS "${JPLCZ_MICROFMT_PORTING_SRC}")
            configure_file(
                "${JPLCZ_MICROFMT_PORTING_SRC}"
                "${JPLCZ_MICROFMT_PORTING_DEST_DIR}/${JPLCZ_MICROFMT_PORTING_HEADER_NAME}"
                COPYONLY
            )
            set(${JPLCZ_MICROFMT_PORTING_HEADER_VAR} ON)
            message(STATUS
                "jplcz_microfmt: using custom porting header '${JPLCZ_MICROFMT_PORTING_HEADER_NAME}' "
                "from ${JPLCZ_MICROFMT_PORTING_SRC} (defining ${JPLCZ_MICROFMT_PORTING_HEADER_VAR})")
        endif()
    endforeach()

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generated_porting_config.hpp.in"
        "${JPLCZ_MICROFMT_PORTING_DEST_DIR}/generated_config.hpp"
        @ONLY
    )

    # microfmt's own source tree never ships a same-named, non-`.template.hpp`
    # file under detail/porting/ (only *.template.hpp scaffolds do), so
    # there is no real file to collide with here regardless of search
    # order -- these include directories only ever add files, never shadow
    # one already found via the plain source include/ tree above.
    #
    # Two -I directories are needed here, not one: microfmt_config.hpp
    # (directly under include/microfmt/, like reloco_config.hpp is
    # directly under include/reloco/) spells its own generated_config.hpp
    # pickup as #include "detail/porting/generated_config.hpp" (no
    # leading "../"), which needs the staging root itself on the search
    # path; logger.hpp (one level deeper, at include/microfmt/log/) needs
    # the "../"-offset "log" subdirectory explained above. Both spellings
    # must resolve to the same "${JPLCZ_MICROFMT_PORTING_STAGING_DIR}/
    # detail/porting/" files.
    target_include_directories(jplcz_microfmt INTERFACE
        $<BUILD_INTERFACE:${JPLCZ_MICROFMT_PORTING_STAGING_DIR}>
        $<BUILD_INTERFACE:${JPLCZ_MICROFMT_PORTING_STAGING_DIR}/log>
    )
endif()

# Header-only INTERFACE forwarders for the MICROFMT_SHARED/
# MICROFMT_SHARED_BUILD compile definitions (see detail/compat.hpp and
# docs/shared-library.md) -- they do not build or export an actual shared
# object themselves; a caller that wants a real `.so`/`.dll` remains
# responsible for its own build target (typically a single translation
# unit including microfmt_compile.hpp), linked against
# jplcz_microfmt::microfmt-shared-export, with every ordinary consumer of
# that library linking against jplcz_microfmt::microfmt-shared instead.
#
# jplcz_reloco::reloco-shared (RELOCO_SHARED only, never
# jplcz_reloco::reloco-shared-export/RELOCO_SHARED_BUILD) is forwarded
# alongside MICROFMT_SHARED: microfmt embeds reloco as an ordinary header
# dependency and never builds reloco's own shared library itself, so
# jplcz_microfmt_shared_export must remain an ordinary RELOCO_SHARED
# *consumer* of reloco, exactly like every other MICROFMT_SHARED consumer
# -- linking reloco-shared-export here instead would wrongly make this
# translation unit emit reloco's own non-template backend definitions
# (RELOCO_API-guarded bodies in mutex.hpp/heap_allocator.hpp/...) as if it
# were the one canonical translation unit building reloco's own shared
# library, which it is not.
add_library(jplcz_microfmt_shared INTERFACE)
target_link_libraries(jplcz_microfmt_shared INTERFACE jplcz_microfmt jplcz_reloco::reloco-shared)
target_compile_definitions(jplcz_microfmt_shared INTERFACE MICROFMT_SHARED)
set_target_properties(jplcz_microfmt_shared PROPERTIES EXPORT_NAME microfmt-shared)

add_library(jplcz_microfmt::microfmt-shared ALIAS jplcz_microfmt_shared)

add_library(jplcz_microfmt_shared_export INTERFACE)
target_link_libraries(jplcz_microfmt_shared_export INTERFACE jplcz_microfmt_shared)
target_compile_definitions(jplcz_microfmt_shared_export INTERFACE MICROFMT_SHARED_BUILD)
set_target_properties(jplcz_microfmt_shared_export PROPERTIES EXPORT_NAME microfmt-shared-export)

add_library(jplcz_microfmt::microfmt-shared-export ALIAS jplcz_microfmt_shared_export)

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

        if(STANDARD GREATER_EQUAL 20)
            if(TARGET PkgConfig::JPLCZ_MICROFMT_TIZEN_DLOG)
                target_link_libraries(jplcz_microfmt_headers_cxx${STANDARD} PRIVATE
                    PkgConfig::JPLCZ_MICROFMT_TIZEN_DLOG)
            else()
                # Real dlog.h isn't available here; opt the header-only
                # compile check out of it so tizen_dlog_sink.hpp falls back
                # to its host-side stub instead of failing to find <dlog.h>.
                target_compile_definitions(jplcz_microfmt_headers_cxx${STANDARD} PRIVATE
                    MICROFMT_COMPILE_WITHOUT_TIZEN_DLOG=1)
            endif()
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
        tests/test_reloco_formatters.cpp
        tests/test_reloco_debug.cpp
        tests/test_std_debug.cpp
        tests/test_debug_float.cpp
        tests/test_fmt_priority.cpp
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
        tests/test_sbuf_sink.cpp
        tests/test_c_string_span_sink.cpp
        tests/test_memory_buffer.cpp
        tests/test_sink_provider.cpp)
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

    # Verifies that MICROFMT_DISABLE_DEBUG_FLAG actually strips the implicit
    # `{:?}` support (see docs/porting.md) without affecting Debug<T>/
    # as_debug(); built as its own binary since the macro must be defined
    # before microfmt.hpp's first inclusion for the whole translation unit.
    add_executable(jplcz_microfmt_disable_debug_flag_tests
        tests/test_disable_debug_flag.cpp)
    target_link_libraries(jplcz_microfmt_disable_debug_flag_tests PRIVATE
        jplcz_microfmt GTest::gtest_main)
    target_compile_definitions(jplcz_microfmt_disable_debug_flag_tests PRIVATE
        MICROFMT_DISABLE_DEBUG_FLAG)
    jplcz_microfmt_enable_warnings(jplcz_microfmt_disable_debug_flag_tests)
    gtest_discover_tests(jplcz_microfmt_disable_debug_flag_tests
        TEST_PREFIX "DisableDebugFlag.")

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
        TARGETS jplcz_microfmt jplcz_microfmt_shared jplcz_microfmt_shared_export
        EXPORT jplcz_microfmtTargets
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )

    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        COMPONENT Development
    )

    # Installed after (never before) the plain include/ tree above so its
    # contents -- always just the *.template.hpp scaffolds under
    # detail/porting/ -- exist first; this only ever adds the real,
    # non-template files a JPLCZ_MICROFMT_PORTING_HEADERS build staged
    # alongside them, never replaces anything.
    if(JPLCZ_MICROFMT_PORTING_HEADERS)
        install(
            DIRECTORY "${JPLCZ_MICROFMT_PORTING_DEST_DIR}/"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/microfmt/detail/porting"
            COMPONENT Development
        )
    endif()

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