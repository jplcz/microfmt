// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file fp_unwinder.hpp
 * @brief Frame-pointer-based unwinder backend utilizing register_context_ref.
 */

#include "address_space.hpp"
#include "dwarf_abi.hpp"
#include "frame_pointer.hpp"
#include "register_context.hpp"
#include <cstdint>

namespace microfmt {

/**
 * @brief Context used by a frame-pointer unwinder.
 *
 * @tparam AbiTraits Architecture-specific frame layout traits.
 */
template <typename AbiTraits> struct fp_unwinder_context {
  /// Address space containing the stack to unwind.
  address_space_ref space;
};

/**
 * @brief Tag selecting the frame-pointer unwinder for an ABI.
 * @tparam AbiTraits Architecture-specific frame layout traits.
 */
template <typename AbiTraits> struct fp_unwinder_tag {};

} // namespace microfmt

template <typename AbiTraits>
struct microfmt::frame_unwinder_traits<microfmt::fp_unwinder_tag<AbiTraits>> {
  /**
   * @brief Context type accepted by the frame unwinder.
   */
  using context_type = microfmt::fp_unwinder_context<AbiTraits>;

  /**
   * @brief Reads the caller frame record from the current frame pointer
   * register.
   *
   * The ABI traits provide the saved frame-pointer and return-address offsets
   * along with the standard frame pointer register index (`fp_reg`).
   *
   * @param context Required borrow of the unwinder context.
   * @param reg_ctx Target register context handle.
   * @param current_pc Unused: pure frame-pointer-chain walking needs no
   * unwind-descriptor lookup, so it never consults the current PC.
   * @param next_fp Receives the caller's frame pointer.
   * @param next_pc Receives the normalized caller program counter.
   * @return `true` when both frame slots were read and form a valid caller
   * frame; otherwise `false`.
   */
  static bool step(value_ref<const context_type> context,
                   register_context_ref reg_ctx, uintptr_t /*current_pc*/,
                   uintptr_t &next_fp, uintptr_t &next_pc) noexcept {
    if (!reg_ctx)
      return false;

    // Read current frame pointer dynamically from register context using
    // AbiTraits. On architectures whose frame-pointer register is fixed
    // (all but ARM), this is just AbiTraits::fp_reg; on ARM it depends on
    // the live ARM/Thumb instruction-set state (see
    // arm_abi_traits::resolve_fp_reg).
    const uint32_t fp_reg = resolve_fp_register<AbiTraits>(reg_ctx);
    typename AbiTraits::register_type raw_fp = 0;
    if (!reg_ctx.read_raw(fp_reg, &raw_fp, AbiTraits::pointer_size)) {
      return false;
    }

    uintptr_t current_fp = static_cast<uintptr_t>(raw_fp);
    if (current_fp == 0)
      return false;

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

    if (!context->space.read_bytes(fp_addr, &saved_fp, ptr_size))
      return false;
    if (!context->space.read_bytes(ra_addr, &saved_ra, ptr_size))
      return false;

    if (saved_fp <= current_fp || saved_ra == 0) {
      return false;
    }

    next_fp = static_cast<uintptr_t>(saved_fp);
    next_pc = AbiTraits::normalize_pc(static_cast<uintptr_t>(saved_ra));
    return true;
  }
};