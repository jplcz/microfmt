// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "address_space.hpp"
#include "dwarf_abi.hpp"
#include "frame_pointer.hpp"
#include <cstdint>

namespace microfmt {

template <typename AbiTraits> struct fp_unwinder_context {
  address_space_ref space;
};

template <typename AbiTraits> struct fp_unwinder_tag {};

} // namespace microfmt

template <typename AbiTraits>
struct microfmt::frame_unwinder_traits<microfmt::fp_unwinder_tag<AbiTraits>> {
  using context_type = microfmt::fp_unwinder_context<AbiTraits>;

  static bool step(const void *ctx, uintptr_t current_fp, uintptr_t &next_fp,
                   uintptr_t &next_pc) noexcept {
    if (!ctx || current_fp == 0)
      return false;
    const auto &cfg = *static_cast<const context_type *>(ctx);

    constexpr size_t ptr_size = AbiTraits::pointer_size;

    if ((current_fp % ptr_size) != 0)
      return false;

    typename AbiTraits::register_type saved_fp = 0;
    typename AbiTraits::register_type saved_ra = 0;

    // Read stack slots using architecture-specific offsets from AbiTraits
    uintptr_t fp_addr = static_cast<uintptr_t>(
        static_cast<ptrdiff_t>(current_fp) + AbiTraits::fp_slot_offset);
    uintptr_t ra_addr = static_cast<uintptr_t>(
        static_cast<ptrdiff_t>(current_fp) + AbiTraits::ra_slot_offset);

    if (!cfg.space.read_bytes(fp_addr, &saved_fp, ptr_size))
      return false;
    if (!cfg.space.read_bytes(ra_addr, &saved_ra, ptr_size))
      return false;

    if (saved_fp <= current_fp || saved_ra == 0) {
      return false;
    }

    next_fp = static_cast<uintptr_t>(saved_fp);
    next_pc = static_cast<uintptr_t>(
        saved_ra & ~static_cast<typename AbiTraits::register_type>(1));
    return true;
  }
};