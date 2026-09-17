// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once
/** @file chained_unwinder.hpp @brief Cascaded/tiered frame unwinder combining
 * EXIDX, DWARF CFI, frame pointer, and unwind-hint strategies. */

#include "dwarf_abi.hpp"
#include "frame_pointer.hpp"
#include "register_context.hpp"
#include "unwind_hint.hpp"
#include <cstdint>

namespace microfmt {

template <typename AbiTraits> struct chained_unwinder_context {
  address_space_ref space;
  frame_unwinder_ref exidx_unwinder{};
  frame_unwinder_ref dwarf_unwinder{};
  frame_unwinder_ref fp_unwinder{};
  unwind_hint_registry_ref hints{};
};

template <typename AbiTraits> struct chained_unwinder_tag {};

template <typename AbiTraits>
struct frame_unwinder_traits<chained_unwinder_tag<AbiTraits>> {
  using context_type = chained_unwinder_context<AbiTraits>;

  /**
   * @brief Walks one frame by trying EXIDX, DWARF, frame-pointer, then hints.
   *
   * Hint routines receive the same mutable register context used by the other
   * unwinder tiers. They may read or update GPRs and other target registers.
   */
  static bool step(value_ref<const context_type> context,
                   register_context_ref reg_ctx,
                   uintptr_t &next_fp, uintptr_t &next_pc) noexcept {
    if (!reg_ctx)
      return false;

    uintptr_t trial_fp = 0;
    uintptr_t trial_pc = 0;

    if (context->exidx_unwinder &&
        context->exidx_unwinder.step(reg_ctx, trial_fp, trial_pc)) {
      next_fp = trial_fp;
      next_pc = trial_pc;
      return true;
    }

    if (context->dwarf_unwinder &&
        context->dwarf_unwinder.step(reg_ctx, trial_fp, trial_pc)) {
      next_fp = trial_fp;
      next_pc = trial_pc;
      return true;
    }

    if (context->fp_unwinder &&
        context->fp_unwinder.step(reg_ctx, trial_fp, trial_pc)) {
      next_fp = trial_fp;
      next_pc = trial_pc;
      return true;
    }

    if (!context->hints)
      return false;

    typename AbiTraits::register_type raw_pc = 0;
    if (!reg_ctx.read_raw(AbiTraits::ra_reg, &raw_pc,
                          AbiTraits::pointer_size) || raw_pc == 0)
      return false;

    const uintptr_t current_pc =
        AbiTraits::normalize_pc(static_cast<uintptr_t>(raw_pc));
    unwind_hint hint{};
    if (!context->hints.find_hint(current_pc, hint) || !hint.routine)
      return false;

    // The hint receives reg_ctx directly. Do not snapshot only FP/PC here:
    // non-standard unwinders can keep addresses in, and recover values from,
    // any GPR. The routine may also mutate the register context in-place.
    return hint.routine(context->space, reg_ctx, next_fp, next_pc);
  }
};

} // namespace microfmt
