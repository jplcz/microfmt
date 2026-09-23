// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file arm_exidx_unwinder.hpp @brief Frame unwinder driven by the ARM EXIDX
 * exception-unwind tables. */

#include "arm_exidx_decoder.hpp"
#include "arm_exidx_search.hpp"
#include "arm_extab_stream.hpp"
#include "dwarf_abi.hpp"
#include "elf_enumerator.hpp"
#include "frame_pointer.hpp"
#include <cstdint>

namespace microfmt {

// ============================================================================
// ARM EXIDX Table Entry Format
// ============================================================================

/**
 * @brief One `.ARM.exidx` table entry.
 */
struct MICROFMT_API_CLASS arm_exidx_entry {
  /// Relative (PREL31) offset to the function start.
  uint32_t prel31_addr_offset; // Relative offset to function start
  /// Inline bytecode or pointer to `.ARM.extab`.
  uint32_t unwind_data; // Inline bytecode or pointer to .ARM.extab
};

// ============================================================================
// EXIDX Context Interface
// ============================================================================

/**
 * @brief Immutable context describing the EXIDX unwinder back to the
 * type-erased handle.
 */
struct MICROFMT_API_CLASS arm_exidx_unwinder_context {
  /// Address space to unwind in.
  address_space_ref space;
  /// ELF image enumerator used to bind PCs to modules.
  elf_image_enumerator_ref enumerator;
  /// Caller-supplied off-stack storage for the located ELF image.
  elf_image_info *elf_img_storage{nullptr};
};

/**
 * @brief Tag selecting the EXIDX unwinder in the traits customization point.
 *
 * @tparam AbiTraits ABI traits providing the SP/LR/FP register indices (see
 * @ref arm_abi_traits). Defaulted since ARM32 is the only architecture with a
 * `.ARM.exidx`/`.ARM.extab` format, but templated (rather than hardcoded on
 * `dwarf::arm32`) so the SP/LR register-number source of truth lives in one
 * place shared with the DWARF and frame-pointer unwinder backends.
 */
template <typename AbiTraits = arm_abi_traits> struct arm_exidx_unwinder_tag {};

// ============================================================================
// EXIDX Unwinder Traits Implementation
// ============================================================================

/**
 * @brief Specializes @ref frame_unwinder_traits for the EXIDX unwinder.
 */
template <typename AbiTraits>
struct frame_unwinder_traits<arm_exidx_unwinder_tag<AbiTraits>> {
  /// Stateful context type.
  using context_type = arm_exidx_unwinder_context;

  /**
   * @brief Unwinds one frame using the module's `.ARM.exidx` tables and
   * register_context_ref.
   *
   * @param context The @ref arm_exidx_unwinder_context.
   * @param reg_ctx Target register context handle.
   * @param current_pc Program counter of the frame being unwound *from*,
   * supplied by the caller/iterator. Used only to locate the `.ARM.exidx`
   * entry covering this frame -- callers no longer need to pre-seed LR (or
   * any other register) with it before the first step. The *real* LR/SP/FP
   * register values (read via @p reg_ctx, using @p AbiTraits) are still
   * exactly what gets restored/returned as the caller's frame.
   * @param next_fp Receives the caller's frame pointer.
   * @param next_pc Receives the caller's program counter.
   * @return `true` on success.
   */
  static bool step(value_ref<const context_type> context,
                   register_context_ref reg_ctx, uintptr_t current_pc,
                   uintptr_t &next_fp, uintptr_t &next_pc) noexcept {
    if (!reg_ctx)
      return false;

    if (!context->elf_img_storage || !context->enumerator)
      return false;

    // Read the current SP from register context; the untouched hardware LR
    // is kept as the fallback return address for frames whose bytecode
    // never explicitly restores it (see below).
    typename AbiTraits::register_type current_sp = 0;
    typename AbiTraits::register_type return_lr = 0;
    if (!reg_ctx.read_raw(AbiTraits::sp_reg, &current_sp,
                          AbiTraits::pointer_size))
      return false;
    if (!reg_ctx.read_raw(AbiTraits::ra_reg, &return_lr,
                          AbiTraits::pointer_size))
      return false;

    uintptr_t fault_pc = AbiTraits::normalize_pc(current_pc);
    uintptr_t virtual_sp = static_cast<uintptr_t>(current_sp);
    bool unwind_applied = false;

    elf_image_info &img = *context->elf_img_storage;
    img = {};

    // Query which loaded ELF module owns the faulting program counter
    if (context->enumerator.find_by_pc(fault_pc, img) && img.has_exidx()) {
      uintptr_t exidx_start = img.exidx_start;
      uintptr_t exidx_end = img.exidx_end;
      size_t num_entries = (exidx_end - exidx_start) / 8;

      size_t low = 0;
      size_t high = num_entries;
      size_t match_index = num_entries;

      // Perform binary search within this specific module's EXIDX section
      while (low < high) {
        size_t mid = low + (high - low) / 2;
        uintptr_t entry_addr = exidx_start + (mid * 8);
        uint32_t prel31 = 0;
        if (!context->space.read_bytes(entry_addr, &prel31, 4))
          break;

        uintptr_t fn_addr = exidx_table_searcher::decode_prel31(entry_addr, prel31);
        if (fn_addr <= fault_pc) {
          match_index = mid;
          low = mid + 1;
        } else {
          high = mid;
        }
      }

      if (match_index != num_entries) {
        uintptr_t word2_address = exidx_start + (match_index * 8) + 4;
        uint32_t raw_unwind_data = 0;

        if (context->space.read_bytes(word2_address, &raw_unwind_data, 4)) {
          if (raw_unwind_data == 0x1)
            return false;

          // Per the ARM EHABI (#6.2 Generic Model / #6.3 Compact Model): bit
          // 31 SET means the unwind opcodes are packed inline in this very
          // word (compact model, personality index in bits 27:24); bit 31
          // CLEAR means this word is instead a PREL31-encoded pointer to an
          // out-of-line entry in `.ARM.extab` (generic model).
          if ((raw_unwind_data & 0x80000000U) != 0U) {
            RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

            unwind_applied =
                arm_exidx_bytecode_decoder::execute_bytecode(
                    context->space, raw_unwind_data, virtual_sp, fault_pc,
                    reg_ctx);

            RELOCO_END_UNSAFE_BUFFER_USAGE;
          } else {
            uintptr_t extab_addr = exidx_table_searcher::decode_prel31(word2_address, raw_unwind_data);
            unwind_applied = extab_stream_executor::execute(
                context->space, extab_addr, virtual_sp, reg_ctx);
          }
        }
      }
    }

    if (!unwind_applied)
      return false;

    const auto updated_sp = static_cast<typename AbiTraits::register_type>(virtual_sp);
    if (!reg_ctx.write_raw(AbiTraits::sp_reg, &updated_sp, AbiTraits::pointer_size))
      return false;

    // The frame-pointer register itself depends on the live ARM/Thumb
    // instruction-set state (R11 vs. R7); consult AbiTraits rather than
    // assuming R11, so Thumb-compiled frames report the correct value.
    const uint32_t fp_reg = resolve_fp_register<AbiTraits>(reg_ctx);
    typename AbiTraits::register_type updated_fp = 0;
    if (!reg_ctx.read_raw(fp_reg, &updated_fp, AbiTraits::pointer_size)) {
      updated_fp = updated_sp;
    }

    typename AbiTraits::register_type updated_lr = 0;
    if (!reg_ctx.read_raw(AbiTraits::ra_reg, &updated_lr, AbiTraits::pointer_size))
      return false;

    next_fp = static_cast<uintptr_t>(updated_fp);
    next_pc = AbiTraits::normalize_pc(static_cast<uintptr_t>(updated_lr));
    return true;
  }
};

/**
 * @brief Self-contained holder owning the EXIDX scratch storage.
 *
 * Non-copyable/non-movable to guarantee stable interior pointers.
 *
 * @tparam AbiTraits ABI traits forwarded to @ref arm_exidx_unwinder_tag;
 * defaults to @ref arm_abi_traits.
 */
template <typename AbiTraits = arm_abi_traits> struct arm_exidx_unwinder_holder {
  /// Storage for the located ELF image.
  elf_image_info img_storage{};
  /// Unwinder context referencing the owned storage.
  arm_exidx_unwinder_context ctx;

  /**
   * @brief Constructs a holder bound to an address space and enumerator.
   * @param space Address space to unwind in.
   * @param enumerator ELF image enumerator.
   */
  arm_exidx_unwinder_holder(address_space_ref space, elf_image_enumerator_ref enumerator) noexcept
      : ctx{space, enumerator, &img_storage} {}

  /// Copying is disabled to ensure pointer stability.
  arm_exidx_unwinder_holder(const arm_exidx_unwinder_holder &) = delete;
  /// Copy-assignment is disabled to ensure pointer stability.
  arm_exidx_unwinder_holder &operator=(const arm_exidx_unwinder_holder &) = delete;
  /// Moving is disabled to ensure pointer stability.
  arm_exidx_unwinder_holder(arm_exidx_unwinder_holder &&) = delete;
  /// Move-assignment is disabled to ensure pointer stability.
  arm_exidx_unwinder_holder &operator=(arm_exidx_unwinder_holder &&) = delete;

  /**
   * @brief Builds a type-erased unwinder handle bound to this holder.
   * @return An @ref frame_unwinder_ref over @ref ctx.
   */
  [[nodiscard]] frame_unwinder_ref make_ref() & noexcept {
    return frame_unwinder_ref(arm_exidx_unwinder_tag<AbiTraits>{}, ctx);
  }
};

} // namespace microfmt