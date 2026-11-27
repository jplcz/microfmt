// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file arch_aarch64.hpp
 * @brief Architecture policy for 64-bit ARM (AArch64) cores, for `elf_core_writer`.
 *
 * Why: AArch64 targets (Cortex-A, and Cortex-R/M profiles in 64-bit mode) use the
 * Linux `user_pt_regs` layout in `NT_PRSTATUS`: x0-x30, the stack pointer `sp`,
 * the program counter `pc` and the processor state `pstate`. Registers are read
 * by DWARF (debugging format) number; unreadable ones are stored as zero.
 * Floating-point/SIMD registers are written only by `arch_aarch64_fpu`, as the `NT_PRFPREG` register set.
 */

#include "../inspector/dwarf_registers.hpp"
#include "elf_format.hpp"

namespace microfmt::coredump {

template <bool WithFpu = false> struct arch_aarch64_t {
  static constexpr bool is_64bit = true;
  static constexpr bool little_endian = true;
  static constexpr std::uint16_t machine = 183; // EM_AARCH64
  static constexpr std::uint32_t flags = 0;
  static constexpr std::uint32_t page_size = 4096;
  static constexpr std::size_t prstatus_size = 392;
  static constexpr std::size_t regset_count = WithFpu ? 1 : 0;

  /** @brief `NT_PRFPREG`: v0-v31 (16 bytes each), then the 32-bit `fpsr` and `fpcr` status registers. */
  static constexpr core_regset regset(std::size_t) noexcept { return {nt_prfpreg, "CORE", 528}; }

  template <typename Emitter> static void write_prstatus(Emitter &out, const core_thread &thread) noexcept {
    detail::write_prstatus_prefix<true>(out, thread);
    detail::write_register_slots<true>(out, thread, register_slots,
                                       [](std::uint32_t slot) noexcept { return dwarf_number(slot); });
    out.u32(0); // floating point registers not included
    out.u32(0);
  }

private:
  static constexpr std::uint32_t register_slots = 34; // x0-x30, sp, pc, pstate

  static constexpr std::uint32_t dwarf_number(std::uint32_t slot) noexcept {
    if (slot <= 32)
      return slot; // x0-x30, sp (31) and pc (32) are numbered consecutively
    return static_cast<std::uint32_t>(dwarf::aarch64::pstate);
  }
};

using arch_aarch64 = arch_aarch64_t<false>;
using arch_aarch64_fpu = arch_aarch64_t<true>;

} // namespace microfmt::coredump
