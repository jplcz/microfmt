// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file microfmt_compile.hpp
 * @brief Umbrella header for the single translation unit that builds a
 * MICROFMT_SHARED library (see microfmt/detail/compat.hpp and
 * microfmt/microfmt_config.hpp for MICROFMT_SHARED/MICROFMT_SHARED_BUILD).
 *
 * Every microfmt entity with a genuine build/consume split (core:
 * vformat_to, int_formatter_specs::parse/format_int_impl -- see
 * microfmt.hpp/microfmt.ipp; formatters: format_hexdump, formatter<
 * std::error_category>/<std::error_code>, formatter<semver>, formatter<
 * styled_str_view>, formatter<raw_ptr_view>, formatter<escaped_view>,
 * formatter<bitfield_view>, detail::format_uuid_bytes -- see their
 * respective formatters/*.hpp/*.ipp pairs, and any more added over time)
 * only gets its out-of-line *definition* compiled into a translation unit
 * that both defines MICROFMT_SHARED_BUILD *and* includes the header
 * declaring that entity. Consumers only ever need the individual formatter
 * headers they actually use (exactly as in the default header-only build);
 * this header exists purely so the *library build* TU doesn't have to
 * track and repeat that same list of formatter headers by hand as more of
 * them gain MICROFMT_SHARED support -- one:
 *
 *   #define MICROFMT_SHARED
 *   #define MICROFMT_SHARED_BUILD
 *   #include <microfmt/microfmt_compile.hpp>
 *
 * ...as the sole content of one .cpp, built `-shared`, is enough to produce
 * a library exporting every migrated symbol below, regardless of which
 * subset of them any given consumer ends up using.
 *
 * Requires MICROFMT_SHARED_BUILD (and, transitively, MICROFMT_SHARED) to
 * already be defined before this header is included -- including it in an
 * ordinary header-only or MICROFMT_SHARED-consumer build is almost
 * certainly a mistake (it would needlessly drag in every optional formatter
 * header), so it is rejected at compile time instead.
 */

#if !defined(MICROFMT_SHARED_BUILD)
#error "microfmt_compile.hpp is only meant for the translation unit that builds a MICROFMT_SHARED library -- define MICROFMT_SHARED and MICROFMT_SHARED_BUILD before including it (see microfmt/detail/compat.hpp)"
#endif

#include "microfmt.hpp"

// Add further formatter headers here as they gain MICROFMT_SHARED support
// (i.e. as soon as any of their entities switch from a plain `inline`
// definition to MICROFMT_API + a sibling *.ipp file); consumers are
// unaffected either way, since they only include what they actually use.
#include "formatters/bitfield.hpp"
#include "formatters/error.hpp"
#include "formatters/escaped.hpp"
#include "formatters/hexdump.hpp"
#include "formatters/pointer.hpp"
#include "formatters/semver.hpp"
#include "formatters/styled.hpp"
#include "formatters/uuid.hpp"
