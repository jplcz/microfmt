// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file dl_symbol_resolver.hpp @brief `dladdr`-backed
 * @ref microfmt::symbol_resolver_ref implementation for Linux and BSD. */

#pragma once

#if !defined(__linux__) && !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__NetBSD__) &&                 \
    !defined(__DragonFly__)
#error "microfmt/inspector/dl_symbol_resolver.hpp only supports Linux and BSD systems"
#endif

#include <cstdint>
#include <dlfcn.h>

#include "../microfmt.hpp"
#include "symbol_resolver.hpp"

namespace microfmt::detail {

/**
 * @brief Resolves @p addr to its nearest preceding symbol/image via
 * `dladdr`.
 * @param addr Address to resolve.
 * @param out_raw Receives the raw resolution result.
 * @return `true` when `dladdr` located at least an owning image.
 */
inline bool dl_resolve_symbol(uintptr_t addr, raw_resolved_symbol &out_raw) noexcept {
  if (addr == 0)
    return false;

  Dl_info info{};
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  if (::dladdr(reinterpret_cast<const void *>(addr), &info) == 0 || info.dli_fbase == nullptr)
    return false;

  raw_resolved_symbol result{};
  result.image_name = (info.dli_fname && info.dli_fname[0] != '\0') ? string_view(info.dli_fname) : string_view("");
  result.image_load_base = reinterpret_cast<uintptr_t>(info.dli_fbase);

  if (info.dli_sname && info.dli_sname[0] != '\0' && info.dli_saddr != nullptr) {
    result.symbol_name = string_view(info.dli_sname);
    result.symbol_base = reinterpret_cast<uintptr_t>(info.dli_saddr);
    result.is_exact = (result.symbol_base == addr);
  }

  out_raw = result;
  return true;
}

} // namespace microfmt::detail

namespace microfmt {

/**
 * @brief Tag selecting the `dladdr`-backed @ref symbol_resolver_traits
 * specialization.
 */
struct dl_symbol_resolver_tag {};

/**
 * @brief Traits binding @ref dl_symbol_resolver_tag to `dladdr`-based symbol
 * resolution.
 *
 * Stateless: `dladdr` queries the dynamic linker's link map directly and
 * requires no per-instance context. `symbol_name`/`image_name` in the
 * returned record point to storage owned by the dynamic linker and remain
 * valid for as long as the owning image stays loaded; @p scratch is unused.
 */
template <> struct symbol_resolver_traits<dl_symbol_resolver_tag> {
  using context_type = void;

  static bool resolve(uintptr_t addr, span<char>, raw_resolved_symbol &out_raw) noexcept {
    return detail::dl_resolve_symbol(addr, out_raw);
  }
};

} // namespace microfmt
