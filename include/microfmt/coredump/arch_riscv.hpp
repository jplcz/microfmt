// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file arch_riscv.hpp
 * @brief Architecture policies for 32-bit and 64-bit RISC-V cores, for `elf_core_writer`.
 *
 * Why: RISC-V is common on microcontrollers (32-bit, "RV32") and application
 * cores (64-bit, "RV64"). Both use the Linux `user_regs_struct` register block in
 * `NT_PRSTATUS`: the program counter followed by x1-x31 (x0 is always zero and
 * is not stored). Registers are read by DWARF (debugging format) number;
 * unreadable ones are stored as zero. Floating-point registers are written only by the `_fpu` aliases, as the
 * `NT_PRFPREG` register set.
 */

#include "../inspector/dwarf_registers.hpp"
#include "elf_format.hpp"

namespace microfmt::coredump {

/**
 * @brief RISC-V policy. Floating point (`WithFpu`) and, for a non-zero @p VlenBytes, the RVV (RISC-V Vector
 * extension) registers as `NT_RISCV_VECTOR`: `vstart`, `vl`, `vtype`, `vcsr` and `vlenb` (one word each) followed
 * by `v0`-`v31`, @p VlenBytes bytes each (the hardware's VLEN divided by 8).
 */
template <bool Is64, bool WithFpu = false, std::uint32_t VlenBytes = 0> struct arch_riscv_t {
  static constexpr bool is_64bit = Is64;
  static constexpr bool little_endian = true;
  static constexpr std::uint16_t machine = 243; // EM_RISCV
  static constexpr std::uint32_t flags = 0;
  static constexpr std::uint32_t page_size = 4096;
  static constexpr std::size_t prstatus_size = Is64 ? 376 : 204;
  static constexpr std::size_t regset_count = (WithFpu ? 1U : 0U) + (VlenBytes != 0 ? 1U : 0U);

  /** @brief `NT_PRFPREG`: f0-f31 as 64-bit values (the "D" double-precision extension), then 32-bit `fcsr`. */
  static constexpr core_regset regset(std::size_t index) noexcept {
    if (WithFpu && index == 0)
      return {nt_prfpreg, "CORE", 264};
    return {nt_riscv_vector, "LINUX", detail::rvv_regset_size(VlenBytes, Is64 ? 8U : 4U)};
  }

  template <typename Emitter> static void write_prstatus(Emitter &out, const core_thread &thread) noexcept {
    detail::write_prstatus_prefix<Is64>(out, thread);
    detail::write_register_slots<Is64>(out, thread, register_slots,
                                       [](std::uint32_t slot) noexcept { return dwarf_number(slot); });
    if constexpr (Is64) {
      out.u32(0); // floating point registers not included
      out.u32(0);
    } else {
      out.u32(0);
    }
  }

private:
  static constexpr std::uint32_t register_slots = 32; // pc, x1-x31

  static constexpr std::uint32_t dwarf_number(std::uint32_t slot) noexcept {
    return slot == 0 ? static_cast<std::uint32_t>(dwarf::riscv::pc) : slot;
  }
};

using arch_riscv32 = arch_riscv_t<false>;
using arch_riscv64 = arch_riscv_t<true>;
using arch_riscv32_fpu = arch_riscv_t<false, true>;
using arch_riscv64_fpu = arch_riscv_t<true, true>;

} // namespace microfmt::coredump
