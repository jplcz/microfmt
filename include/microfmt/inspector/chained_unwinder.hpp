#pragma once

/** @file chained_unwinder.hpp @brief Cascaded/tiered frame unwinder combining
 * EXIDX, DWARF CFI, frame pointer, and unwind-hint strategies. */

#include "frame_pointer.hpp"
#include "unwind_hint.hpp"
#include <cstdint>

namespace microfmt {

struct chained_unwinder_context {
  address_space_ref space;

  // Pluggable unwinder tiers (any can be left empty/null)

  // clang-format off
  frame_unwinder_ref exidx_unwinder{}; // Tier 1: ARM EXIDX
  frame_unwinder_ref dwarf_unwinder{}; // Tier 2: DWARF CFI (.debug_frame / .eh_frame)
  frame_unwinder_ref fp_unwinder{};    // Tier 3: Standard Frame Pointer
  // clang-format on

  unwind_hint_registry_ref hints{}; // Type-erased unwind hint registry

  // Pointer to the current instruction pointer (PC) being unwound,
  // allowing hint lookup without probing a non-existent/malformed stack frame.
  uintptr_t *current_pc{nullptr};
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
    // Attempt Secondary: DWARF CFI Unwinder
    // ========================================================================
    if (cfg.dwarf_unwinder &&
        cfg.dwarf_unwinder.step(current_fp, trial_fp, trial_pc)) {
      next_fp = trial_fp;
      next_pc = trial_pc;
      return true;
    }

    // ========================================================================
    // Attempt Tertiary: Standard Frame Pointer Unwinder
    // ========================================================================
    if (cfg.fp_unwinder &&
        cfg.fp_unwinder.step(current_fp, trial_fp, trial_pc)) {
      next_fp = trial_fp;
      next_pc = trial_pc;
      return true;
    }

    // ========================================================================
    // Attempt Quaternary: Unwind Hint Table Fallback (Direct PC Lookup)
    // ========================================================================
    if (cfg.hints && cfg.current_pc && *cfg.current_pc != 0) {
      unwind_hint hint{};
      if (cfg.hints.find_hint(*cfg.current_pc, hint)) {
        if (hint.routine != nullptr) {
          // Delegate entirely to the hint's custom assembly/stub decoding
          // routine
          return hint.routine(cfg.space, current_fp, *cfg.current_pc, next_fp,
                              next_pc);
        }
      }
    }

    return false; // All strategies failed
  }
};

} // namespace microfmt