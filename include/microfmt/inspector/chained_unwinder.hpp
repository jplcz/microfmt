#pragma once

#include "frame_pointer.hpp"
#include "unwind_hint.hpp"
#include <cstdint>

namespace microfmt {

struct chained_unwinder_context {
  address_space_ref space;
  frame_unwinder_ref exidx_unwinder; // EXIDX / DWARF unwinder handle
  frame_unwinder_ref fp_unwinder;    // Frame pointer unwinder handle
  unwind_hint_registry_ref hints;    // Type-erased unwind hint registry
};

struct chained_unwinder_tag {};

template <> struct frame_unwinder_traits<chained_unwinder_tag> {
  using context_type = chained_unwinder_context;

  static bool step(const void *ctx, uintptr_t current_fp, uintptr_t &next_fp,
                   uintptr_t &next_pc) noexcept {
    if (!ctx || current_fp == 0)
      return false;
    const auto &cfg = *static_cast<const chained_unwinder_context *>(ctx);

    uintptr_t trial_fp = 0;
    uintptr_t trial_pc = 0;

    // ========================================================================
    // Attempt Primary: ARM EXIDX Unwinder
    // ========================================================================
    if (cfg.exidx_unwinder &&
        cfg.exidx_unwinder.step(current_fp, trial_fp, trial_pc)) {
      next_fp = trial_fp;
      next_pc = trial_pc;
      return true;
    }

    // ========================================================================
    // Attempt Secondary: Standard Frame Pointer Unwinder
    // ========================================================================
    if (cfg.fp_unwinder &&
        cfg.fp_unwinder.step(current_fp, trial_fp, trial_pc)) {
      next_fp = trial_fp;
      next_pc = trial_pc;
      return true;
    }

    // ========================================================================
    // Attempt Tertiary: Type-Erased Unwind Hint Table Fallback
    // ========================================================================
    // Read return address (LR) from current frame to evaluate hint match
    uint32_t return_lr = 0;
    if (!cfg.space.read_bytes(current_fp + 4, &return_lr, 4)) {
      if (!cfg.space.read_bytes(current_fp, &return_lr, 4))
        return false;
    }
    uintptr_t fault_pc = static_cast<uintptr_t>(return_lr & ~1U);

    if (cfg.hints) {
      unwind_hint hint{};
      if (cfg.hints.find_hint(fault_pc, hint)) {
        // Apply manual hint recovery rule
        uint32_t saved_fp = 0;
        uint32_t saved_lr = 0;

        if (cfg.space.read_bytes(current_fp, &saved_fp, 4) &&
            cfg.space.read_bytes(current_fp + 4, &saved_lr, 4)) {
          next_fp = static_cast<uintptr_t>(saved_fp);
          next_pc = static_cast<uintptr_t>(saved_lr & ~1U);
          return true;
        }

        // If FP slot is broken, apply fixed stack offset from hint
        next_fp = current_fp + hint.sp_offset;
        next_pc = fault_pc;
        return true;
      }
    }

    return false; // All strategies failed
  }
};

} // namespace microfmt