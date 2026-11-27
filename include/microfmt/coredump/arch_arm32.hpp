// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file arch_arm32.hpp
 * @brief Shared 32-bit ARM policy behind `arch_arm_cortex_m` and `arch_arm_cortex_a`.
 *
 * Why: Cortex-M and 32-bit Cortex-A use the same Linux ARM `NT_PRSTATUS` layout (r0-r15, a program status register
 * and the "original r0" slot) and the same `NT_ARM_VFP` register set. They differ only in the page size granule
 * used to find holes in memory and in which register holds the program status (`xPSR` on Cortex-M, `cpsr` on
 * Cortex-A). Keeping one implementation avoids two copies drifting apart.
 *
 * With @p WithVfp the policy also writes the `NT_ARM_VFP` register set (VFP: Vector Floating Point unit): the 32
 * double-precision registers followed by the 32-bit `fpscr` (floating-point status and control register).
 * Registers that cannot be read are stored as zero.
 */

#include "../inspector/dwarf_registers.hpp"
#include "elf_format.hpp"

namespace microfmt::coredump::detail {

template <bool WithVfp, bool WithTls, std::uint32_t Breakpoints, std::uint32_t Watchpoints>
inline constexpr regset_list<4> arm32_sets = [] {
  regset_list<4> list;
  list.add({nt_arm_vfp, "LINUX", WithVfp ? 260U : 0U});
  list.add({nt_arm_tls, "LINUX", WithTls ? 4U : 0U});
  list.add({nt_arm_hw_break, "LINUX", hwdebug_state_size(Breakpoints, 8)});
  list.add({nt_arm_hw_watch, "LINUX", hwdebug_state_size(Watchpoints, 8)});
  return list;
}();

/** @brief Shared 32-bit ARM policy; `WithTls` adds the 32-bit thread pointer and the counts add hardware debug state.
 */
template <std::uint32_t PageSize, std::uint32_t StatusRegister, bool WithVfp, bool WithTls = false,
          std::uint32_t Breakpoints = 0, std::uint32_t Watchpoints = 0>
struct arm32_policy {
  static constexpr bool is_64bit = false;
  static constexpr bool little_endian = true;
  static constexpr std::uint16_t machine = 40;       // EM_ARM
  static constexpr std::uint32_t flags = 0x05000000; // EF_ARM_EABI_VER5
  static constexpr std::uint32_t page_size = PageSize;
  static constexpr std::size_t prstatus_size = 148;
  static constexpr std::size_t regset_count = arm32_sets<WithVfp, WithTls, Breakpoints, Watchpoints>.count;

  static constexpr core_regset regset(std::size_t index) noexcept {
    return arm32_sets<WithVfp, WithTls, Breakpoints, Watchpoints>.items[index];
  }

  /** @brief Writes the body of an `NT_PRSTATUS` note (Linux ARM `elf_prstatus`) for @p thread. */
  template <typename Emitter> static void write_prstatus(Emitter &out, const core_thread &thread) noexcept {
    write_prstatus_prefix<false>(out, thread);
    write_register_slots<false>(out, thread, register_slots,
                                [](std::uint32_t slot) noexcept { return dwarf_number(slot); });
    out.u32(0); // floating point registers are not in this note
  }

private:
  static constexpr std::uint32_t register_slots = 18;

  static constexpr std::uint32_t dwarf_number(std::uint32_t slot) noexcept {
    if (slot < 16)
      return slot; // r0-r15 share their DWARF numbers
    return slot == 16 ? StatusRegister : no_dwarf_register;
  }
};

} // namespace microfmt::coredump::detail
