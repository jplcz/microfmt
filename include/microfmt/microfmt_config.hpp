// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file microfmt_config.hpp
 * @brief Single build-time customization entry point for every optional
 * microfmt feature macro.
 *
 * This header is included first, before anything else, by
 * `microfmt/detail/compat.hpp` (the library's common ancestor header), which
 * makes it the earliest point at which configuration can be injected
 * regardless of which microfmt header an application includes first.
 *
 * User overrides must not be made by editing this file. Instead, define
 * `MICROFMT_CONFIG` (via a compiler `-D` flag, e.g.
 * `-DMICROFMT_CONFIG=1`) to opt in to including a header named
 * `microfmt_user_config.hpp`, which must be reachable on the compiler's
 * include search path (e.g. in an application-owned include directory listed
 * before microfmt's own `include/` in the include path). When
 * `MICROFMT_CONFIG` is defined, `microfmt_user_config.hpp` is included here,
 * before any library header defines its own default, so every `#define` it
 * contains takes precedence over the library defaults below and over the
 * individual `#ifndef`-guarded defaults each feature header applies on its
 * own.
 *
 * Every macro below may alternatively be set directly with a compiler `-D`
 * flag instead of (or in addition to) `microfmt_user_config.hpp`; both
 * approaches are equivalent since library headers only ever apply a default
 * when the macro is not already defined.
 *
 * See docs/porting.md for the full contract of each option.
 */

#if defined(MICROFMT_CONFIG)
#include "microfmt_user_config.hpp"
#endif

// Picks up whichever MICROFMT_*_BACKEND_CUSTOM macro(s) the
// JPLCZ_MICROFMT_PORTING_HEADERS CMake build baked into a generated
// detail/porting/generated_config.hpp (see CMakeLists.txt and
// detail/porting/README.md) -- a no-op (file never generated, #include
// never taken) unless that CMake variable was set. Mirrors reloco's own
// reloco_config.hpp pickup of detail/porting/generated_config.hpp;
// unlike that header, microfmt_config.hpp has no ordering constraint
// against __has_include's availability (microfmt already depends on
// reloco, whose detail/compat.hpp establishes RELOCO_HAS_INCLUDE well
// before microfmt_config.hpp is reachable), but a bare __has_include
// check is used here anyway for exact parity with reloco_config.hpp.
#if defined(__has_include)
#if __has_include("detail/porting/generated_config.hpp")
#include "detail/porting/generated_config.hpp"
#endif
#endif

// ============================================================================
// Available customization points (see docs/porting.md for details)
// ============================================================================
//
// RELOCO_KERNEL, RELOCO_KERNEL_PANIC, RELOCO_TRAP, RELOCO_UNREACHABLE /
// RELOCO_HAS_UNREACHABLE, RELOCO_DISABLE_ASSERT / RELOCO_DISABLE_ASSERT_STDIO,
// RELOCO_DEBUG, RELOCO_TLS_MODEL
//     Owned by reloco (see reloco/reloco_config.hpp, reloco/tls_provider.hpp);
//     microfmt uses them directly rather than a MICROFMT_* alias. They may
//     still be overridden here, since microfmt_user_config.hpp (see
//     MICROFMT_CONFIG above) is included before any header applies its own
//     default.
//
// MICROFMT_USE_SYSTEM_ERROR
//     Selects the microfmt::posix_errno message backend: std::error_code /
//     std::system_category() (1) or strerror_s/strerror_r/strerror (0,
//     default).
//
// MICROFMT_DISABLE_DEBUG_FLAG
//     Strips the runtime `{:?}` (Rust Debug flag) support out of
//     formatter<char>/formatter<microfmt::string_view>/
//     formatter<std::string_view>/detail::const_char_like at the
//     preprocessor level: a literal `{:?}` in a format string then behaves
//     like `{}` for those types (no quoting/escaping), removing that
//     dead-otherwise code from the binary on targets that never write
//     `{:?}` in a format string. Does not affect reloco::Debug<T>/
//     microfmt::as_debug(), which never depend on this at all -- as_debug()
//     always prefers a type's Debug<T> when one exists, entirely
//     independent of this macro. See docs/porting.md.
//
// MICROFMT_ENABLE_BOOST_UUID / MICROFMT_ENABLE_BOOST_SOURCE_LOCATION
//     Enable the optional Boost-dependent formatters/detection. The caller
//     is responsible for ensuring Boost is available.
//
// MICROFMT_ENABLE_DEFAULT_LOGGER / MICROFMT_DEFAULT_LOGGER
//     Enable the process-wide built-in default logger, and/or name the
//     logger expression used by the MICROFMT_LOG_* free-function macros.
//
// MICROFMT_DEFAULT_LOG_SINK_BACKEND_CUSTOM
//     Replaces MICROFMT_ENABLE_DEFAULT_LOGGER's built-in stdout_color_sink
//     backend with a fixed-path #include "detail/porting/
//     default_log_sink.hpp" (not shipped -- see detail/porting/README.md
//     and detail/porting/default_log_sink.template.hpp), for targets that
//     need a different default (an OS-native logging facility, an
//     RTOS/bare-metal console, ...). See docs/porting.md.
//
// MICROFMT_SHARED / MICROFMT_SHARED_BUILD
//     Opt in to building/consuming a subset of microfmt's "heavy"
//     definitions (see e.g. microfmt.ipp and formatters/{hexdump,error,
//     semver,styled,pointer,escaped,bitfield,uuid}.ipp) as a real
//     shared library instead of duplicating them into every translation
//     unit/shared object that includes microfmt's headers. MICROFMT_SHARED
//     must be defined consistently by every translation unit in the
//     program; exactly one of them -- the one building the actual shared
//     library -- must additionally define MICROFMT_SHARED_BUILD, and
//     should simply `#include <microfmt/microfmt_compile.hpp>` (an
//     umbrella header pulling in every migrated entity's out-of-line
//     definitions) instead of hand-picking headers. See
//     microfmt/detail/compat.hpp (MICROFMT_API, MICROFMT_API_CONSTEXPR)
//     for the full contract, and tools/codesize/testbed/README.md for the
//     cross-.so duplication problem this addresses.
//
//     microfmt's own formatter<T> specializations that are themselves
//     templated over a consumer-supplied T (formatter<std::optional<T>>,
//     formatter<reloco::vector<T>>, ...) cannot be migrated this way --
//     microfmt doesn't know which T a given application will instantiate.
//     For those, see microfmt/microfmt_extern.hpp's opt-in
//     MICROFMT_FORMATTER_INSTANCE(Type), which lets the *application*
//     explicitly instantiate/extern-declare the specific Types it uses.
//
// MICROFMT_UNWIND_HINT_POINTER_SIZE
//     Pointer size (4 or 8) used by the assembler-only helpers in
//     microfmt/inspector/unwind_hint_asm.h when __SIZEOF_POINTER__ is
//     unavailable. Must match the target's uintptr_t/function-pointer
//     representation.
