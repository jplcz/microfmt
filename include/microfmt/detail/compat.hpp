// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file compat.hpp
 * @brief Compiler, language-version, and optional-library feature detection.
 *
 * Core compiler/language feature-detection macros (`RELOCO_CXX17`,
 * `RELOCO_HAS_STD_SPAN`, `RELOCO_NODISCARD`, `RELOCO_TRAP`, ...) are defined
 * by `reloco` (see `<reloco/detail/compat.hpp>`) and used directly; this
 * header only adds microfmt-specific feature detection reloco has no need
 * for (source_location, std::expected, Boost UUID, POSIX fd, Android log). */

#include "../microfmt_config.hpp"

#include <reloco/detail/compat.hpp>

#if RELOCO_CXX20 && RELOCO_HAS_INCLUDE(<source_location>)
#include <source_location>
#endif

#if RELOCO_CXX20 && defined(__cpp_lib_source_location) && (__cpp_lib_source_location >= 201907L)
#define MICROFMT_HAS_STD_SOURCE_LOCATION 1
#else
#define MICROFMT_HAS_STD_SOURCE_LOCATION 0
#endif

#if RELOCO_CXX23 && RELOCO_HAS_INCLUDE(<expected>)
#include <expected>
#endif

#if RELOCO_CXX23 && defined(__cpp_lib_expected) && (__cpp_lib_expected >= 202202L)
#define MICROFMT_HAS_STD_EXPECTED 1
#else
#define MICROFMT_HAS_STD_EXPECTED 0
#endif

#if defined(MICROFMT_ENABLE_BOOST_SOURCE_LOCATION)
// clang-format off
#if RELOCO_HAS_INCLUDE(<boost/assert/source_location.hpp>)
#include <boost/assert/source_location.hpp>
// clang-format on
#define MICROFMT_HAS_BOOST_SOURCE_LOCATION 1
#else
#error "MICROFMT_ENABLE_BOOST_SOURCE_LOCATION requires <boost/assert/source_location.hpp>"
#endif
#else
#define MICROFMT_HAS_BOOST_SOURCE_LOCATION 0
#endif

#if MICROFMT_HAS_STD_SOURCE_LOCATION || MICROFMT_HAS_BOOST_SOURCE_LOCATION
#define MICROFMT_HAS_SOURCE_LOCATION 1
#else
#define MICROFMT_HAS_SOURCE_LOCATION 0
#endif

#if defined(MICROFMT_ENABLE_BOOST_UUID) || defined(BOOST_UUID_UUID_HPP) || defined(BOOST_UUID_HPP)
#if !defined(BOOST_UUID_UUID_HPP) && !defined(BOOST_UUID_HPP)
// clang-format off
#if RELOCO_HAS_INCLUDE(<boost/uuid/uuid.hpp>)
#include <boost/uuid/uuid.hpp>
// clang-format on
#else
#error "MICROFMT_ENABLE_BOOST_UUID requires <boost/uuid/uuid.hpp>"
#endif
#endif
#define MICROFMT_HAS_BOOST_UUID 1
#else
#define MICROFMT_HAS_BOOST_UUID 0
#endif

#if defined(__unix__) || defined(__APPLE__) || defined(__posix)
#define MICROFMT_HAS_POSIX_FD 1
#else
#define MICROFMT_HAS_POSIX_FD 0
#endif

#if defined(__ANDROID__)
#define MICROFMT_HAS_ANDROID_LOG 1
#else
#define MICROFMT_HAS_ANDROID_LOG 0
#endif

// ============================================================================
// Shared-library build/consume support (MICROFMT_SHARED)
// ============================================================================
//
// microfmt is header-only by default: even the "heavy", non-templated
// definitions factored out into a `*.ipp` file (see e.g. `microfmt.ipp`,
// included from `microfmt.hpp`) are plain `inline` and live directly in the
// header, so -- like every other microfmt entity -- each independently
// compiled translation unit gets its own copy of them, and the linker only
// merges duplicate copies within a single link step, never across separate
// shared-object boundaries. For an application built as one executable
// plus several plugin `.so`'s that each `#include` microfmt, this means
// every plugin pays for its own copy of the same handful of routines (see
// `tools/codesize/testbed/README.md` for measured numbers).
//
// Define MICROFMT_SHARED (to any value, before including any microfmt
// header, e.g. via `microfmt_user_config.hpp`/`MICROFMT_CONFIG` or a
// compiler `-D` flag applied consistently to every translation unit in the
// program) to switch every `*.ipp`-hosted definition to a plain
// declaration instead: ordinary consumers then link against one shared
// definition rather than each instantiating their own copy.
//
// Exactly one translation unit in the whole program -- the one building
// the actual shared library that is meant to host these definitions --
// must additionally define MICROFMT_SHARED_BUILD (to any value) before
// including any microfmt header. That TU alone re-imports the `*.ipp`
// bodies as exported, out-of-line definitions; every other TU (which
// defines MICROFMT_SHARED but not MICROFMT_SHARED_BUILD) only sees
// declarations and must be linked against that library.
//
// `MICROFMT_API` decorates every entity affected by this split:
//   - MICROFMT_SHARED not defined (default):
//     `MICROFMT_API` -> `inline`; current, unchanged header-only behavior.
//   - MICROFMT_SHARED defined, MICROFMT_SHARED_BUILD not defined (consume):
//     `MICROFMT_API` -> plain declaration, no body
//     (+ `__declspec(dllimport)` on MSVC).
//   - MICROFMT_SHARED and MICROFMT_SHARED_BUILD both defined (build):
//     `MICROFMT_API` -> exported, out-of-line definition
//     (`__declspec(dllexport)` on MSVC, default visibility elsewhere).
//
// `MICROFMT_SHARED_PROVIDE_DEFINITIONS` is 1 exactly when the current TU
// should pull in `*.ipp` bodies at all (default header-only mode, or the
// MICROFMT_SHARED_BUILD library-build TU) and 0 when it should only see
// declarations (ordinary MICROFMT_SHARED consumer).
//
// This is opt-in and off by default: a plain header-only build (the
// overwhelming common case, and every existing consumer) is entirely
// unaffected.
#if !defined(MICROFMT_SHARED)
#define MICROFMT_API inline
#define MICROFMT_SHARED_PROVIDE_DEFINITIONS 1
#elif defined(MICROFMT_SHARED_BUILD)
#if defined(_MSC_VER)
#define MICROFMT_API __declspec(dllexport)
#elif RELOCO_HAS_ATTRIBUTE(visibility)
#define MICROFMT_API __attribute__((visibility("default")))
#else
#define MICROFMT_API
#endif
#define MICROFMT_SHARED_PROVIDE_DEFINITIONS 1
#else
#if defined(_MSC_VER)
#define MICROFMT_API __declspec(dllimport)
#else
#define MICROFMT_API
#endif
#define MICROFMT_SHARED_PROVIDE_DEFINITIONS 0
#endif

// MICROFMT_API_CONSTEXPR is MICROFMT_API for an entity that is `constexpr`
// in the default header-only build (where MICROFMT_API is plain `inline`,
// so adding `constexpr` costs nothing and preserves compile-time
// callability exactly as before) but must drop `constexpr` under
// MICROFMT_SHARED: a `constexpr` function is implicitly `inline`, which
// would force every MICROFMT_SHARED_BUILD-declared-only consumer to still
// carry a definition, defeating the whole build/consume split.
#if !defined(MICROFMT_SHARED)
#define MICROFMT_API_CONSTEXPR constexpr MICROFMT_API
#else
#define MICROFMT_API_CONSTEXPR MICROFMT_API
#endif

// MICROFMT_API_CLASS decorates a whole class/struct (as opposed to
// MICROFMT_API, which decorates individual members/free functions) so its
// vtable and any members implicitly emitted per-TU (typeinfo, defaulted
// special members, ...) are deduplicated across a MICROFMT_SHARED_BUILD
// library's shared-object boundary, exactly like MICROFMT_API does for
// out-of-line function definitions.
//
// A plain `class`/`struct` head cannot be decorated `inline` (unlike a
// function or variable), so MICROFMT_API_CLASS -- unlike MICROFMT_API --
// expands to nothing in the default header-only build: there is no
// separate shared object to deduplicate against, and every member stays
// implicitly inline the same way it already does today.
#if !defined(MICROFMT_SHARED)
#define MICROFMT_API_CLASS
#elif defined(MICROFMT_SHARED_BUILD)
#if defined(_MSC_VER)
#define MICROFMT_API_CLASS __declspec(dllexport)
#elif RELOCO_HAS_ATTRIBUTE(visibility)
#define MICROFMT_API_CLASS __attribute__((visibility("default")))
#else
#define MICROFMT_API_CLASS
#endif
#else
#if defined(_MSC_VER)
#define MICROFMT_API_CLASS __declspec(dllimport)
#else
#define MICROFMT_API_CLASS
#endif
#endif

// dlog availability cannot be reliably inferred from a compiler-predefined
// macro (unlike MICROFMT_HAS_ANDROID_LOG's __ANDROID__ check); headers
// assume the real Tizen SDK is available unless the includer opts out via
// MICROFMT_COMPILE_WITHOUT_TIZEN_DLOG (see CMakeLists.txt).
#if defined(MICROFMT_COMPILE_WITHOUT_TIZEN_DLOG)
#define MICROFMT_HAS_DLOG 0
#else
#define MICROFMT_HAS_DLOG 1
#endif
