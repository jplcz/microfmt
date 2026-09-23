// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause
//
// Application-authored RELOCO_TYPE_INSTANCE/MICROFMT_FORMATTER_INSTANCE
// list for the testbed's own generated call sites (see
// generate_modules.py's SHAPE_CATALOG) -- see docs/shared-library.md (in
// both this repository and reloco's) for what these macros do and why
// they're application code, not library code.
//
// Included from both shared_common.cpp (the RELOCO_SHARED_BUILD/
// MICROFMT_SHARED_BUILD translation unit -- generates the definitions
// below) and common.hpp (every ordinary RELOCO_SHARED/MICROFMT_SHARED
// consumer TU -- generates the matching `extern template`
// declarations), so the two lists can never drift apart. Included after
// testbed::point3 and its formatter<> specialization are defined (see
// common.hpp), since reloco::expected<testbed::point3, ...> needs it.
//
// Only entities enabled by RELOCO_SHARED/MICROFMT_SHARED consistently
// across the whole "multiso (shared common .so)" build (see run.sh) reach
// this file's macros at all -- everywhere else (the plain single-exe/
// single-.so table, and multiso's "naive" variant) neither RELOCO_SHARED
// nor MICROFMT_SHARED is defined, so every macro below expands to nothing
// and this header has zero effect, exactly as in a normal header-only
// build.
#pragma once

#include <microfmt/formatters/monad.hpp>
#include <microfmt/formatters/ranges.hpp>
#include <microfmt/formatters/reloco.hpp>
#include <microfmt/formatters/string.hpp>
#include <microfmt/microfmt_extern.hpp>
#include <reloco/reloco_extern.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <utility>

// Class templates instantiated by generate_modules.py's SHAPE_CATALOG (see
// common.hpp) that are themselves reloco containers/wrappers over a
// concrete, testbed-defined type -- reloco has no way to know about these
// ahead of time (see reloco/reloco_extern.hpp), so the testbed lists them
// here exactly as any other application would.
RELOCO_TYPE_INSTANCE(reloco::optional<int>);
RELOCO_TYPE_INSTANCE(reloco::vector<int>);
RELOCO_TYPE_INSTANCE(reloco::flat_map<int, int>);
RELOCO_TYPE_INSTANCE(reloco::non_zero<std::uint16_t>);
RELOCO_TYPE_INSTANCE(reloco::checked<int>);
RELOCO_TYPE_INSTANCE(reloco::saturating<std::uint8_t>);
RELOCO_TYPE_INSTANCE(reloco::wrapping<std::uint8_t>);

// reloco::expected<testbed::point3, std::error_code> is deliberately *not*
// listed as a RELOCO_TYPE_INSTANCE: whole-class explicit instantiation
// instantiates every member function, including operator==, which
// requires testbed::point3 to be equality-comparable -- it isn't (see
// common.hpp), and there's no reason to add that just for this. The
// MICROFMT_FORMATTER_INSTANCE below only needs format()/has_value()/
// value(), so it works fine without a matching RELOCO_TYPE_INSTANCE.

// reloco_compile.hpp (see shared_common.cpp) already bundles
// RELOCO_TYPE_INSTANCE for reloco's own basic_string/basic_sso_string/
// basic_string_view<char> for the RELOCO_SHARED_BUILD side -- listing them
// again here would be a duplicate explicit-instantiation *definition* in
// that same translation unit. Ordinary consumer TUs (this testbed's
// module_*.cpp, via common.hpp/reloco::sso_string) never include
// reloco_compile.hpp themselves (see its own doc comment for why), so they
// still need their own `extern template class` declaration for these --
// guard this half to only the consumer side.
#if defined(RELOCO_SHARED) && !defined(RELOCO_SHARED_BUILD)
RELOCO_TYPE_INSTANCE(reloco::basic_string<char>);
RELOCO_TYPE_INSTANCE(reloco::basic_sso_string<char>);
RELOCO_TYPE_INSTANCE(reloco::basic_string_view<char>);
#endif

// microfmt::formatter<T> specializations templated over one of the
// concrete Types above (or over std::optional<int>/std::error_code, which
// microfmt itself cannot pre-migrate either -- see
// microfmt/microfmt_extern.hpp). format_type_thunk<T> is the type-erased
// trampoline every microfmt::format_to<Args...> call site actually goes
// through, so instantiating it here is what dedupes the call site itself,
// not just formatter<T>'s own body.
MICROFMT_FORMATTER_INSTANCE(std::optional<int>);
MICROFMT_FORMATTER_INSTANCE(reloco::optional<int>);
MICROFMT_FORMATTER_INSTANCE(reloco::vector<int>);
MICROFMT_FORMATTER_INSTANCE(reloco::flat_map<int, int>);
MICROFMT_FORMATTER_INSTANCE(reloco::checked<int>);
MICROFMT_FORMATTER_INSTANCE(reloco::saturating<std::uint8_t>);
MICROFMT_FORMATTER_INSTANCE(reloco::wrapping<std::uint8_t>);
// microfmt's own concrete (non-app-templated) formatters -- semver,
// escaped_view, etc. are library-defined types, so microfmt_compile.hpp
// already migrated their formatter<T>::format()/parse() bodies out of
// line (see microfmt.ipp/formatters/*.ipp) and MICROFMT_SHARED alone
// dedupes those. format_type_thunk<T> (the type-erased trampoline every
// microfmt::format_to<Args...> call site instantiates) is a *different*
// template that microfmt has no way to pre-instantiate on its own for
// every possible T (built-in or its own) -- so even these concrete,
// library-owned types still need their own MICROFMT_FORMATTER_INSTANCE
// here, same as an app-templated Type.
MICROFMT_FORMATTER_INSTANCE(int);
MICROFMT_FORMATTER_INSTANCE(unsigned int);
MICROFMT_FORMATTER_INSTANCE(microfmt::semver);
MICROFMT_FORMATTER_INSTANCE(microfmt::escaped_view);
MICROFMT_FORMATTER_INSTANCE(microfmt::bitfield_view);
MICROFMT_FORMATTER_INSTANCE(microfmt::uuid_view);
MICROFMT_FORMATTER_INSTANCE(microfmt::hexdump_view);
MICROFMT_FORMATTER_INSTANCE(microfmt::styled_str_view);
MICROFMT_FORMATTER_INSTANCE(microfmt::raw_ptr_view);
MICROFMT_FORMATTER_INSTANCE(std::error_code);
MICROFMT_FORMATTER_INSTANCE(reloco::ordering);
MICROFMT_FORMATTER_INSTANCE(reloco::non_zero<std::uint16_t>);
MICROFMT_FORMATTER_INSTANCE(testbed::point3);
MICROFMT_FORMATTER_INSTANCE(reloco::expected<testbed::point3, std::error_code>);
MICROFMT_FORMATTER_INSTANCE(std::string);
MICROFMT_FORMATTER_INSTANCE(std::string_view);
MICROFMT_FORMATTER_INSTANCE(reloco::basic_sso_string<char>);

// microfmt::join(...)'s return type, join_view<It, Sentinel>, is templated
// over the range's own iterator type -- these are the exact iterator
// types generate_modules.py's _std_vector_join/_std_array_join shapes
// produce (see common.hpp/generate_modules.py); using decltype here
// instead of hand-writing the (compiler/libstdc++-specific,
// __gnu_cxx::__normal_iterator-wrapped) mangled names keeps this correct
// across toolchains without duplicating that detail.
using tb_vector_join_iterator = decltype(std::declval<const std::vector<int> &>().begin());
using tb_array_join_iterator = decltype(std::declval<const std::array<int, 4> &>().begin());
MICROFMT_FORMATTER_INSTANCE(microfmt::join_view<tb_vector_join_iterator, tb_vector_join_iterator>);
MICROFMT_FORMATTER_INSTANCE(microfmt::join_view<tb_array_join_iterator, tb_array_join_iterator>);
