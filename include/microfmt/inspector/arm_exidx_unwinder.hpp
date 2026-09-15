// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

/** @file arm_exidx_unwinder.hpp @brief Frame unwinder driven by the ARM EXIDX
 * exception-unwind tables. */

#include "arm_exidx_decoder.hpp"
#include "arm_exidx_search.hpp"
#include "arm_extab_decoder.hpp"
#include "arm_extab_stream.hpp"
#include "elf_enumerator.hpp"
#include "frame_pointer.hpp"
#include <cstdint>

namespace microfmt {

// ============================================================================
// ARM EXIDX Table Entry Format
// ============================================================================

struct arm_exidx_entry {
  uint32_t prel31_addr_offset; // Relative offset to function start
  uint32_t unwind_data;        // Inline bytecode or pointer to .ARM.extab
};

// ============================================================================
// EXIDX Context Interface
// ============================================================================

struct arm_exidx_unwinder_context {
  address_space_ref space;
  elf_image_enumerator_ref enumerator;
  // Caller-supplied off-stack storage for register state
  // (Prevents ~300 bytes of kernel stack allocation per unwind step)
  arm_register_state *reg_scratch{nullptr};
  // Calle supplied off-stack storage
  elf_image_info *elf_img_storage{nullptr};
};

struct arm_exidx_unwinder_tag {};

// ============================================================================
// EXIDX Unwinder Traits Implementation
// ============================================================================

template <> struct frame_unwinder_traits<arm_exidx_unwinder_tag> {
  using context_type = arm_exidx_unwinder_context;

  static bool step(const void *ctx, uintptr_t current_fp, uintptr_t &next_fp,
                   uintptr_t &next_pc) noexcept {
    if (!ctx || current_fp == 0 || (current_fp % 4) != 0)
      return false;
    const auto &cfg = *static_cast<const arm_exidx_unwinder_context *>(ctx);

    uint32_t saved_fp = 0;
    uint32_t return_lr = 0;

    if (!cfg.reg_scratch || !cfg.elf_img_storage)
      return false;
    if (!cfg.space.read_bytes(current_fp, &saved_fp, 4))
      return false;
    if (!cfg.space.read_bytes(current_fp + 4, &return_lr, 4))
      return false;

    if (saved_fp <= current_fp || return_lr == 0)
      return false;

    uintptr_t fault_pc = static_cast<uintptr_t>(return_lr & ~1U);
    uintptr_t virtual_sp = current_fp + 8;
    arm_register_state &regs = *cfg.reg_scratch;
    regs = {};

    if (cfg.enumerator) {
      elf_image_info &img = *cfg.elf_img_storage;
      img = {};

      // Query which loaded ELF module owns the faulting program counter
      if (cfg.enumerator.find_by_pc(fault_pc, img) && img.has_exidx()) {
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
          if (!cfg.space.read_bytes(entry_addr, &prel31, 4))
            break;

          uintptr_t fn_addr =
              exidx_table_searcher::decode_prel31(entry_addr, prel31);
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

          if (cfg.space.read_bytes(word2_address, &raw_unwind_data, 4)) {
            if ((raw_unwind_data & 0x80000000U) != 0U) {
              uintptr_t extab_addr = exidx_table_searcher::decode_prel31(
                  word2_address, raw_unwind_data);
              extab_stream_executor::execute(cfg.space, extab_addr, virtual_sp,
                                             fault_pc, regs);
            } else if (raw_unwind_data != 0x1) {
              arm_exidx_bytecode_decoder::execute_bytecode(
                  cfg.space, raw_unwind_data, virtual_sp, fault_pc, regs);
            }
          }
        }
      }
    }

    next_fp = static_cast<uintptr_t>(saved_fp);
    next_pc = fault_pc;
    return true;
  }
};

struct arm_exidx_unwinder_holder {
  elf_image_info img_storage{};
  arm_register_state reg_scratch{};
  arm_exidx_unwinder_context ctx;

  arm_exidx_unwinder_holder(address_space_ref space,
                            elf_image_enumerator_ref enumerator) noexcept
      : ctx{space, enumerator, &reg_scratch, &img_storage} {}

  arm_exidx_unwinder_holder(const arm_exidx_unwinder_holder &) = delete;
  arm_exidx_unwinder_holder &
  operator=(const arm_exidx_unwinder_holder &) = delete;
  arm_exidx_unwinder_holder(arm_exidx_unwinder_holder &&) = delete;
  arm_exidx_unwinder_holder &operator=(arm_exidx_unwinder_holder &&) = delete;

  [[nodiscard]] frame_unwinder_ref make_ref() noexcept {
    return frame_unwinder_ref(arm_exidx_unwinder_tag{}, ctx);
  }
};

} // namespace microfmt