# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

# jplcz_microfmt_add_reflect_dump(<target>
#     COMPILER <path-or-name>
#     MANIFEST <path>
#     OUTPUT <path>
#     INCLUDE_DIRS <dir> [<dir> ...]
#     [DEFINES <define> [<define> ...]]
#     [FLAGS <flag> [<flag> ...]]
#     [STANDARD <std, default c++26>]
#     [DEPENDS <file> [<file> ...]]
# )
#
# Wires `tools/reflect_dump/reflect_dump_main.cpp` (see docs/reflection.md
# and tools/reflect_dump/README.md) into the build as a custom target named
# <target>: building it (e.g. `cmake --build . --target <target>`) compiles
# the reflect_dump generator, runs it against MANIFEST, and writes the
# generated, reflection-free formatter header to OUTPUT.
#
# This is intentionally *not* an ordinary `add_executable()` + custom
# command built with the project's own CXX toolchain, because that
# toolchain may be a cross-compiler (embedded target, MSVC, an older GCC/
# Clang, ...) that cannot build -- let alone run -- a P2996 `-freflection`
# binary at all. Every step below instead invokes COMPILER directly, by
# its own absolute command line, entirely independent of
# CMAKE_CXX_COMPILER/CMAKE_CROSSCOMPILING/any toolchain file: the compiler
# and its output binary always run on the machine actually performing the
# build (the host), never on/for the cross target, which is the only way
# this can work at all when cross-compiling.
#
# COMPILER must be explicit and is never auto-detected/searched for: a
# guessed "first g++ found on PATH" could easily be the wrong compiler
# (wrong version, or the cross-compiler itself under some toolchain
# files that prepend a target triple to PATH), silently producing either
# a build failure or, worse, incorrect generated code. The caller must
# know and name a real, tested, P2996-capable (`-freflection`) compiler
# (GCC 16+ trunk as of this writing).
#
# INCLUDE_DIRS/DEFINES must likewise be given explicitly and as plain
# paths/tokens, never as generator expressions or CMake target names:
# COMPILER is not a CMake-registered compiler, so ordinary
# target_include_directories()/target_compile_definitions() plumbing and
# $<TARGET_PROPERTY:...> genexes have no meaningful way to reach it.
# Resolve whatever a caller-side target actually uses (e.g. by reading
# `INTERFACE_INCLUDE_DIRECTORIES` at configure time) into concrete
# directory strings before calling this function.
function(jplcz_microfmt_add_reflect_dump TARGET_NAME)
    set(options "")
    set(oneValueArgs COMPILER MANIFEST OUTPUT STANDARD)
    set(multiValueArgs INCLUDE_DIRS DEFINES FLAGS DEPENDS)
    cmake_parse_arguments(ARD "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARD_COMPILER)
        message(FATAL_ERROR
            "jplcz_microfmt_add_reflect_dump(${TARGET_NAME}): COMPILER is "
            "required. Pass the path or name of a P2996-capable "
            "(-freflection) host compiler explicitly, e.g. COMPILER g++-16 "
            "-- this function never auto-detects or searches PATH for one, "
            "since a wrong guess could silently pick a non-capable compiler "
            "(or, when cross-compiling, the cross-compiler itself).")
    endif()
    if(NOT ARD_MANIFEST)
        message(FATAL_ERROR
            "jplcz_microfmt_add_reflect_dump(${TARGET_NAME}): MANIFEST is required "
            "(a header #including every type annotated with "
            "MICROFMT_REFLECT_FORMAT/MICROFMT_REFLECT_DUMP_ENUM).")
    endif()
    if(NOT ARD_OUTPUT)
        message(FATAL_ERROR
            "jplcz_microfmt_add_reflect_dump(${TARGET_NAME}): OUTPUT is required "
            "(the path the generated header is written to).")
    endif()
    if(NOT ARD_INCLUDE_DIRS)
        message(FATAL_ERROR
            "jplcz_microfmt_add_reflect_dump(${TARGET_NAME}): INCLUDE_DIRS is "
            "required -- at minimum the microfmt and jplcz_reloco include "
            "directories, as plain paths (see this file's header comment "
            "for why generator expressions/target names cannot be used here).")
    endif()
    if(NOT ARD_STANDARD)
        set(ARD_STANDARD "c++26")
    endif()

    get_filename_component(ARD_MANIFEST "${ARD_MANIFEST}" ABSOLUTE)
    get_filename_component(ARD_OUTPUT "${ARD_OUTPUT}" ABSOLUTE)

    # Resolve COMPILER to an absolute path. NO_CMAKE_FIND_ROOT_PATH forces
    # an ordinary host PATH search even when a toolchain file has set
    # CMAKE_FIND_ROOT_PATH_MODE_PROGRAM to ONLY/BOTH for cross-compiling:
    # this program must always be resolved on/for the host, never the
    # cross sysroot.
    find_program(JPLCZ_MICROFMT_REFLECT_DUMP_COMPILER_${TARGET_NAME}
        NAMES "${ARD_COMPILER}"
        NO_CMAKE_FIND_ROOT_PATH
        DOC "Host compiler used by the ${TARGET_NAME} reflect_dump target"
    )
    if(NOT JPLCZ_MICROFMT_REFLECT_DUMP_COMPILER_${TARGET_NAME})
        message(FATAL_ERROR
            "jplcz_microfmt_add_reflect_dump(${TARGET_NAME}): COMPILER "
            "'${ARD_COMPILER}' was not found. It must be a real, already "
            "installed P2996-capable (-freflection) host compiler binary "
            "reachable by name on PATH or given as an absolute path.")
    endif()
    set(ARD_COMPILER "${JPLCZ_MICROFMT_REFLECT_DUMP_COMPILER_${TARGET_NAME}}")

    set(reflect_dump_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/reflect_dump")
    get_filename_component(reflect_dump_root "${reflect_dump_root}" ABSOLUTE)
    set(reflect_dump_main "${reflect_dump_root}/reflect_dump_main.cpp")
    if(NOT EXISTS "${reflect_dump_main}")
        message(FATAL_ERROR
            "jplcz_microfmt_add_reflect_dump(${TARGET_NAME}): could not find "
            "reflect_dump_main.cpp at '${reflect_dump_main}'.")
    endif()

    # Probe COMPILER for real -freflection support once, at configure time,
    # so a caller gets one clear, actionable error here instead of an
    # obscure failure later when the generator itself fails to build.
    set(probe_source "${CMAKE_CURRENT_BINARY_DIR}/reflect_dump_${TARGET_NAME}_probe.cpp")
    file(WRITE "${probe_source}" "int main() { return 0; }\n")
    execute_process(
        COMMAND "${ARD_COMPILER}" "-std=${ARD_STANDARD}" -freflection
                -c "${probe_source}"
                -o "${probe_source}.o"
        RESULT_VARIABLE probe_result
        OUTPUT_VARIABLE probe_output
        ERROR_VARIABLE probe_output
    )
    if(NOT probe_result EQUAL 0)
        message(FATAL_ERROR
            "jplcz_microfmt_add_reflect_dump(${TARGET_NAME}): COMPILER "
            "'${ARD_COMPILER}' does not support '-std=${ARD_STANDARD} "
            "-freflection'. A P2996-capable compiler (GCC 16+ trunk as of "
            "this writing) is required. Probe output:\n${probe_output}")
    endif()

    set(include_flags "")
    foreach(dir IN LISTS ARD_INCLUDE_DIRS)
        get_filename_component(dir "${dir}" ABSOLUTE)
        list(APPEND include_flags "-I${dir}")
    endforeach()

    set(define_flags "")
    foreach(def IN LISTS ARD_DEFINES)
        list(APPEND define_flags "-D${def}")
    endforeach()

    # The generator itself is a small, host-only binary: it never ships to
    # users and never runs on/for the cross target, so its name/suffix is
    # based on the host, not CMAKE_EXECUTABLE_SUFFIX (which names the
    # *target* platform's convention when cross-compiling).
    set(generator_suffix "")
    if(CMAKE_HOST_WIN32)
        set(generator_suffix ".exe")
    endif()
    set(generator_exe
        "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_generator${generator_suffix}")

    add_custom_command(
        OUTPUT "${generator_exe}"
        COMMAND "${ARD_COMPILER}"
                "-std=${ARD_STANDARD}" -freflection
                -DMICROFMT_REFLECT_DUMP_MODE
                "-DMICROFMT_REFLECT_DUMP_MANIFEST=\"${ARD_MANIFEST}\""
                ${define_flags}
                ${include_flags}
                ${ARD_FLAGS}
                "${reflect_dump_main}"
                -o "${generator_exe}"
        DEPENDS "${reflect_dump_main}" "${ARD_MANIFEST}" ${ARD_DEPENDS}
        COMMENT "reflect_dump: building generator for ${TARGET_NAME} with ${ARD_COMPILER}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${ARD_OUTPUT}"
        COMMAND "${generator_exe}" "${ARD_OUTPUT}"
        DEPENDS "${generator_exe}"
        COMMENT "reflect_dump: generating ${ARD_OUTPUT}"
        VERBATIM
    )

    add_custom_target(${TARGET_NAME} DEPENDS "${ARD_OUTPUT}")
    set_target_properties(${TARGET_NAME} PROPERTIES
        JPLCZ_MICROFMT_REFLECT_DUMP_OUTPUT "${ARD_OUTPUT}"
    )
endfunction()
