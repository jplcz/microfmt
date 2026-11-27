// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file arch_x86_64.hpp
 * @brief Architecture policy describing x86-64 ELF cores for `elf_core_writer`.
 *
 * Why: the writer itself knows nothing about CPUs. Everything that depends on
 * the architecture (word size, byte order, machine number and how a thread's
 * registers are laid out in the `NT_PRSTATUS` note) lives in a small policy
 * type chosen at compile time. Supporting another CPU means writing another
 * policy like this one; users can also define their own.
 *
 * How the registers are found: the policy reads each slot of the Linux
 * `user_regs_struct` by its DWARF (debugging format) register number through
 * the thread's `register_context_ref`. A register that cannot be read is
 * stored as zero.
 */

#include "elf_format.hpp"

namespace microfmt::coredump {

/**
 * @brief x86-64 policy. With @p WithFpu it also writes the `NT_PRFPREG` register set (512-byte FXSAVE image:
 * x87 and SSE registers), which lets a debugger show `$xmm0` and friends.
 */
template <bool WithFpu = false> struct arch_x86_64_t {
  static constexpr bool is_64bit = true;
  static constexpr bool little_endian = true;
  static constexpr std::uint16_t machine = 62; // EM_X86_64
  static constexpr std::uint32_t flags = 0;
  static constexpr std::uint32_t page_size = 4096;
  static constexpr std::size_t prstatus_size = 336;
  static constexpr std::size_t regset_count = WithFpu ? 1 : 0;

  static constexpr core_regset regset(std::size_t) noexcept { return {nt_prfpreg, "CORE", 512}; }
  /** @brief Writes the body of an `NT_PRSTATUS` note (Linux `elf_prstatus`) for @p thread. */
  template <typename Emitter> static void write_prstatus(Emitter &out, const core_thread &thread) noexcept {
    detail::write_prstatus_prefix<true>(out, thread);
    detail::write_register_slots<true>(out, thread, register_slots,
                                       [](std::uint32_t slot) noexcept { return dwarf_number(slot); });
    out.u32(0); // floating point registers not included
    out.u32(0);
  }

private:
  static constexpr std::uint32_t register_slots = 27;
  static constexpr std::uint32_t no_register = detail::no_dwarf_register;

  // DWARF numbers in `user_regs_struct` order; `no_register` marks a slot with no DWARF register.
  static constexpr std::uint32_t dwarf_number(std::uint32_t slot) noexcept {
    constexpr microfmt::array<std::uint32_t, register_slots> table{
        15, 14, 13, 12, 6, 3, 11, 10, 9, 8, 0, 2, 1, 4, 5, no_register, 16, 51, 49, 7, 52, 58, 59, 53, 50, 54, 55};
    return slot < register_slots ? table[slot] : no_register;
  }
};

using arch_x86_64 = arch_x86_64_t<false>;
using arch_x86_64_fpu = arch_x86_64_t<true>;

} // namespace microfmt::coredump
