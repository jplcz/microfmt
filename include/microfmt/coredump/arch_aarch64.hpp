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

namespace detail {
template <bool WithFpu, bool WithTls, std::uint32_t Breakpoints, std::uint32_t Watchpoints>
inline constexpr regset_list<4> aarch64_sets = [] {
  regset_list<4> list;
  list.add({nt_prfpreg, "CORE", WithFpu ? 528U : 0U});
  list.add({nt_arm_tls, "LINUX", WithTls ? 8U : 0U});
  list.add({nt_arm_hw_break, "LINUX", hwdebug_state_size(Breakpoints, 16)});
  list.add({nt_arm_hw_watch, "LINUX", hwdebug_state_size(Watchpoints, 16)});
  return list;
}();
} // namespace detail

/**
 * @brief AArch64 policy. Besides `NT_PRFPREG` (`WithFpu`), it can write `NT_ARM_TLS` (`WithTls`: the 64-bit
 * `TPIDR_EL0` thread pointer) and the hardware debug state of @p Breakpoints / @p Watchpoints slots
 * (`NT_ARM_HW_BREAK` / `NT_ARM_HW_WATCH`: an 8-byte header plus an address and control word per slot; 0 = off).
 */
template <bool WithFpu = false, bool WithTls = false, std::uint32_t Breakpoints = 0, std::uint32_t Watchpoints = 0>
struct arch_aarch64_t {
  static constexpr bool is_64bit = true;
  static constexpr bool little_endian = true;
  static constexpr std::uint16_t machine = 183; // EM_AARCH64
  static constexpr std::uint32_t flags = 0;
  static constexpr std::uint32_t page_size = 4096;
  static constexpr std::size_t prstatus_size = 392;
  static constexpr std::size_t regset_count = detail::aarch64_sets<WithFpu, WithTls, Breakpoints, Watchpoints>.count;

  /** @brief `NT_PRFPREG` is v0-v31 (16 bytes each), then the 32-bit `fpsr` and `fpcr` status registers. */
  static constexpr core_regset regset(std::size_t index) noexcept {
    return detail::aarch64_sets<WithFpu, WithTls, Breakpoints, Watchpoints>.items[index];
  }

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
