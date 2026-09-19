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

// ============================================================================
// Available customization points (see docs/porting.md for details)
// ============================================================================
//
// RELOCO_KERNEL
//     Define to build for kernel/freestanding targets. Requires
//     RELOCO_KERNEL_PANIC (see below) and disables every hosted-only
//     standard library include across the library.
//
// RELOCO_KERNEL_PANIC(expression, file, line, message)
//     Required when RELOCO_KERNEL is defined. Must be defined as a
//     function-like macro that reports assertion failures through the
//     target's own panic/log facility (printf-like, or a sequence of raw
//     string writes as a last resort).
//
// RELOCO_TRAP()
//     Must not return. Defaults to a compiler debug trap, falling back to
//     std::abort().
//
// RELOCO_UNREACHABLE() / RELOCO_HAS_UNREACHABLE
//     Optimizer hint for unreachable code. Set RELOCO_HAS_UNREACHABLE to 0
//     alongside a no-op RELOCO_UNREACHABLE() if the platform has none.
//
// RELOCO_DISABLE_ASSERT / RELOCO_DISABLE_ASSERT_STDIO
//     Disable RELOCO_ASSERT/RELOCO_DEBUG_ASSERT entirely, or just their
//     default std::fprintf(stderr, ...) diagnostic, respectively.
//
// RELOCO_DEBUG
//     Force RELOCO_DEBUG_ASSERT to stay active even when NDEBUG is
//     defined, instead of compiling down to RELOCO_UNREACHABLE()/no-op.
//
// MICROFMT_TLS_MODEL
//     Selects the microfmt::detail::tls_provider storage model. One of
//     MICROFMT_TLS_MODEL_THREAD_LOCAL (default), MICROFMT_TLS_MODEL_PTHREAD,
//     MICROFMT_TLS_MODEL_SINGLE, MICROFMT_TLS_MODEL_WIN32, or
//     MICROFMT_TLS_MODEL_OS.
//
// MICROFMT_USE_SYSTEM_ERROR
//     Selects the microfmt::posix_errno message backend: std::error_code /
//     std::system_category() (1) or strerror_s/strerror_r/strerror (0,
//     default).
//
// MICROFMT_ENABLE_BOOST_UUID / MICROFMT_ENABLE_BOOST_SOURCE_LOCATION
//     Enable the optional Boost-dependent formatters/detection. The caller
//     is responsible for ensuring Boost is available.
//
// MICROFMT_ENABLE_DEFAULT_LOGGER / MICROFMT_DEFAULT_LOGGER
//     Enable the process-wide built-in default logger, and/or name the
//     logger expression used by the MICROFMT_LOG_* free-function macros.
//
// MICROFMT_UNWIND_HINT_POINTER_SIZE
//     Pointer size (4 or 8) used by the assembler-only helpers in
//     microfmt/inspector/unwind_hint_asm.h when __SIZEOF_POINTER__ is
//     unavailable. Must match the target's uintptr_t/function-pointer
//     representation.
