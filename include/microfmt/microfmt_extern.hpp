// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file microfmt_extern.hpp
 * @brief Opt-in, developer-controlled explicit instantiation of
 * consumer-supplied formatter template instances (e.g. formatter<
 * std::optional<int>>, formatter<reloco::vector<std::string>>) for
 * MICROFMT_SHARED deployments (see microfmt/detail/compat.hpp and
 * microfmt/microfmt_compile.hpp).
 *
 * microfmt_compile.hpp's *.ipp pattern only covers microfmt's own built-in
 * *concrete* formatters (formatter<semver>, formatter<std::error_code>,
 * ...): microfmt itself defines those types, so it can migrate their
 * definitions to a shared library ahead of time. It cannot do the same for
 * formatter<T> specializations that are themselves templates over a
 * consumer-supplied T (formatter<std::optional<T>>, formatter<
 * reloco::vector<T>>, the type-erased format_type_thunk<T> trampoline,
 * ...) -- microfmt has no way to know which T a given application will
 * ever instantiate.
 *
 * MICROFMT_FORMATTER_INSTANCE(Type) closes that gap by letting the
 * *application* (not microfmt) declare, once, which concrete Types it
 * wants deduplicated across shared objects the same way -- e.g. "every
 * shard .so in my deployment formats std::optional<int> and
 * reloco::vector<std::string>, so pull formatter<T>/format_type_thunk<T>
 * for exactly those two Types into the one common shared library instead
 * of duplicating them into every shard".
 *
 * Usage: put the *same* list of MICROFMT_FORMATTER_INSTANCE(...)
 * invocations (after #including whichever formatters/*.hpp header defines
 * formatter<Type> for each Type) in one shared header, included by both:
 *
 *   1. The one MICROFMT_SHARED_BUILD translation unit (typically the same
 *      one that #includes microfmt_compile.hpp) -- there, each invocation
 *      expands to an explicit instantiation *definition*, actually
 *      generating that Type's formatter<Type>/format_type_thunk<Type>
 *      bodies in the shared library.
 *
 *   2. Every ordinary MICROFMT_SHARED consumer translation unit (ones that
 *      do NOT define MICROFMT_SHARED_BUILD) that formats a value of that
 *      Type -- there, each invocation instead expands to an `extern
 *      template` *declaration*, suppressing that translation unit's own
 *      local instantiation and binding it to the shared library's
 *      definition above at link time.
 *
 * Keeping both call sites in perfect sync (same Types, same header) is the
 * caller's responsibility: using a Type with microfmt::format_to in a
 * MICROFMT_SHARED consumer without ever listing it here (or listing it
 * here but never linking against a shared library that was actually built
 * with the matching MICROFMT_SHARED_BUILD instantiation) surfaces as an
 * ordinary "undefined reference" link error, not a silent miscompile.
 *
 * In a plain header-only build (MICROFMT_SHARED not defined at all), this
 * macro expands to nothing: ordinary per-translation-unit implicit
 * instantiation -- already deduplicated by the linker's normal
 * vague-linkage handling within a single binary -- is exactly what you
 * want, and there is no separate shared object to deduplicate *against*.
 */

#include "microfmt.hpp"

#if defined(MICROFMT_SHARED_BUILD)
#define MICROFMT_FORMATTER_INSTANCE(...)                                                                             \
  template struct microfmt::formatter<__VA_ARGS__>;                                                                  \
  template void microfmt::detail::format_type_thunk<__VA_ARGS__>(const void *, microfmt::string_view,               \
                                                                  const microfmt::sink &)
#elif defined(MICROFMT_SHARED)
#define MICROFMT_FORMATTER_INSTANCE(...)                                                                             \
  extern template struct microfmt::formatter<__VA_ARGS__>;                                                          \
  extern template void microfmt::detail::format_type_thunk<__VA_ARGS__>(const void *, microfmt::string_view,        \
                                                                         const microfmt::sink &)
#else
#define MICROFMT_FORMATTER_INSTANCE(...)
#endif
