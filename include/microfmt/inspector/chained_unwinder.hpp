#pragma once

/** @file chained_unwinder.hpp @brief Cascaded/tiered frame unwinder combining
 * EXIDX, DWARF CFI, frame pointer, and unwind-hint strategies. */

#include "dwarf_abi.hpp"
#include "frame_pointer.hpp"
#include "register_context.hpp"
#include "unwind_hint.hpp"
#include <cstdint>

namespace microfmt {

/**
 * @brief Stateful context for the cascaded/tiered unwinder.
 * @tparam AbiTraits Architecture-specific ABI traits.
 */
template <typename AbiTraits> struct chained_unwinder_context {
  /// Address space to unwind in.
  address_space_ref space;

  // Pluggable unwinder tiers (any can be left empty/null)

  // clang-format off
  /// Tier 1: ARM EXIDX unwinder.
  frame_unwinder_ref exidx_unwinder{};
  /// Tier 2: DWARF CFI (.debug_frame / .eh_frame) unwinder.
  frame_unwinder_ref dwarf_unwinder{};
  /// Tier 3: standard frame-pointer unwinder.
  frame_unwinder_ref fp_unwinder{};
  // clang-format on

  /// Type-erased unwind hint registry.
  unwind_hint_registry_ref hints{};
};

/**
 * @brief Tag selecting the chained unwinder in the traits customization point.
 * @tparam AbiTraits Architecture-specific ABI traits.
 */
template <typename AbiTraits> struct chained_unwinder_tag {};

/**
 * @brief Specializes @ref frame_unwinder_traits for the architecture-aware
 * chained unwinder.
 * @tparam AbiTraits Architecture-specific ABI traits.
 */
template <typename AbiTraits>
struct frame_unwinder_traits<chained_unwinder_tag<AbiTraits>> {
  /// Stateful context type.
  using context_type = chained_unwinder_context<AbiTraits>;

  /**
   * @brief Walks one frame by trying each tier in order: EXIDX, DWARF CFI,
   * frame pointer, then unwind-hint lookup using AbiTraits register bindings.
   *
   * @param ctx The @ref chained_unwinder_context.
   * @param reg_ctx Target register context handle.
   * @param next_fp Receives the caller's frame pointer.
   * @param next_pc Receives the caller's program counter.
   * @return `true` when a tier produced the next frame.
   */
  static bool step(const void *ctx, register_context_ref reg_ctx,
                   uintptr_t &next_fp, uintptr_t &next_pc) noexcept {
    if (!ctx || !reg_ctx)
      return false;
    const auto &cfg =
        *static_cast<const chained_unwinder_context<AbiTraits> *>(ctx);

    uintptr_t trial_fp = 0;
    uintptr_t trial_pc = 0;

    // ========================================================================
    // Attempt Primary: ARM EXIDX Unwinder
    // ========================================================================
    if (cfg.exidx_unwinder &&
        cfg.exidx_unwinder.step(reg_ctx, trial_fp, trial_pc)) {
      next_fp = trial_fp;
      next_pc = trial_pc;
      return true;
    }

    // ========================================================================
    // Attempt Secondary: DWARF CFI Unwinder
    // ========================================================================
    if (cfg.dwarf_unwinder &&
        cfg.dwarf_unwinder.step(reg_ctx, trial_fp, trial_pc)) {
      next_fp = trial_fp;
      next_pc = trial_pc;
      return true;
    }

    // ========================================================================
    // Attempt Tertiary: Standard Frame Pointer Unwinder
    // ========================================================================
    if (cfg.fp_unwinder && cfg.fp_unwinder.step(reg_ctx, trial_fp, trial_pc)) {
      next_fp = trial_fp;
      next_pc = trial_pc;
      return true;
    }

    // ========================================================================
    // Attempt Quaternary: Unwind Hint Table Fallback (Direct PC Lookup)
    // ========================================================================
    if (cfg.hints) {
      typename AbiTraits::register_type raw_pc = 0;
      // Use DWARF/architecture standard return address or PC register index
      // from traits
      if (reg_ctx.read_raw(AbiTraits::ra_reg, &raw_pc,
                           AbiTraits::pointer_size) &&
          raw_pc != 0) {
        uintptr_t current_pc =
            AbiTraits::normalize_pc(static_cast<uintptr_t>(raw_pc));

        unwind_hint hint{};
        if (cfg.hints.find_hint(current_pc, hint)) {
          if (hint.routine != nullptr) {
            typename AbiTraits::register_type raw_fp = 0;
            if (!reg_ctx.read_raw(AbiTraits::fp_reg, &raw_fp,
                                  AbiTraits::pointer_size)) {
              raw_fp = 0;
            }
            uintptr_t current_fp = static_cast<uintptr_t>(raw_fp);

            return hint.routine(cfg.space, current_fp, current_pc, next_fp,
                                next_pc);
          }
        }
      }
    }

    return false; // All strategies failed
  }
};

} // namespace microfmt