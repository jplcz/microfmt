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
#include <tuple>

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
   *
   * @param current_pc Program counter of the frame being unwound *from*,
   * supplied by the caller/iterator; forwarded to every tier so none of them
   * need a register pre-seeded with it.
   */
  static bool step(value_ref<const context_type> context,
                   register_context_ref reg_ctx, uintptr_t current_pc,
                   uintptr_t &next_fp, uintptr_t &next_pc) noexcept {
    if (!reg_ctx)
      return false;

    uintptr_t trial_fp = 0;
    uintptr_t trial_pc = 0;

    if (context->exidx_unwinder &&
        context->exidx_unwinder.step(reg_ctx, current_pc, trial_fp,
                                     trial_pc)) {
      next_fp = trial_fp;
      next_pc = trial_pc;
      return true;
    }

    if (context->dwarf_unwinder &&
        context->dwarf_unwinder.step(reg_ctx, current_pc, trial_fp,
                                     trial_pc)) {
      next_fp = trial_fp;
      next_pc = trial_pc;
      return true;
    }

    if (context->fp_unwinder &&
        context->fp_unwinder.step(reg_ctx, current_pc, trial_fp, trial_pc)) {
      next_fp = trial_fp;
      next_pc = trial_pc;
      return true;
    }

    if (!context->hints)
      return false;

    if (current_pc == 0)
      return false;

    unwind_hint hint{};
    if (!context->hints.find_hint(AbiTraits::normalize_pc(current_pc), hint) ||
        !hint.routine)
      return false;

    // The hint receives reg_ctx directly. Do not snapshot only FP/PC here:
    // non-standard unwinders can keep addresses in, and recover values from,
    // any GPR. The routine may also mutate the register context in-place.
    if (!hint.routine(context->space, reg_ctx, next_fp, next_pc))
      return false;

    // reg_ctx is always a mutable scratch snapshot (a dead-process register
    // view or a local ucontext_t/GPR-array copy), never a live process to
    // resume. Hint routines are plain type-erased function pointers with no
    // AbiTraits of their own, so a routine author may reasonably fill only
    // next_fp/next_pc without also writing the fp/ra registers back into
    // reg_ctx; without that write-back, a *subsequent* step() (through this
    // same hint tier, or any other tier that reads the same registers) would
    // silently re-read this frame's now-stale fp/ra and loop forever, as
    // fp_unwinder_tag/dwarf_unwinder_tag did before they were fixed to write
    // their own results back. chained_unwinder_tag knows AbiTraits, so it
    // closes that gap here on every hint routine's behalf, best-effort (a
    // read-only register context still lets this single step succeed via
    // next_fp/next_pc, it just can't chain further through this tier).
    const auto fp_value = static_cast<typename AbiTraits::register_type>(next_fp);
    const auto ra_value = static_cast<typename AbiTraits::register_type>(next_pc);
    std::ignore = reg_ctx.write_raw(resolve_fp_register<AbiTraits>(reg_ctx),
                                    &fp_value, AbiTraits::pointer_size);
    std::ignore = reg_ctx.write_raw(AbiTraits::ra_reg, &ra_value,
                                    AbiTraits::pointer_size);

    return true;
  }
};

} // namespace microfmt
