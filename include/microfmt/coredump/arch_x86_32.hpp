// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file arch_x86_32.hpp
 * @brief Architecture policy for 32-bit x86 (i386) ELF cores, for `elf_core_writer`.
 *
 * Why: 32-bit x86 targets (legacy firmware, bootloaders, embedded PCs) still exist and need an ELF32 core with the
 * Linux i386 `elf_prstatus` layout, which is what `gdb` expects for `EM_386`.
 *
 * Registers are read by their DWARF (debugging format) numbers from the i386 System V ABI (application binary
 * interface); a register that cannot be read is stored as zero.
 *
 * `arch_x86_32_fpu` also writes two register sets: `NT_PRFPREG` (the 108-byte legacy x87 save image) and
 * `NT_PRXFPREG` (the 512-byte FXSAVE image holding the SSE registers).
 */

#include "elf_format.hpp"

namespace microfmt::coredump {

template <bool WithFpu = false> struct arch_x86_32_t {
  static constexpr bool is_64bit = false;
  static constexpr bool little_endian = true;
  static constexpr std::uint16_t machine = 3; // EM_386
  static constexpr std::uint32_t flags = 0;
  static constexpr std::uint32_t page_size = 4096;
  static constexpr std::size_t prstatus_size = 144;
  static constexpr std::size_t regset_count = WithFpu ? 2 : 0;

  static constexpr core_regset regset(std::size_t index) noexcept {
    return index == 0 ? core_regset{nt_prfpreg, "CORE", 108} : core_regset{nt_prxfpreg, "LINUX", 512};
  }

  /** @brief Writes the body of an `NT_PRSTATUS` note (Linux i386 `elf_prstatus`) for @p thread. */
  template <typename Emitter> static void write_prstatus(Emitter &out, const core_thread &thread) noexcept {
    detail::write_prstatus_prefix<false>(out, thread);
    detail::write_register_slots<false>(out, thread, register_slots,
                                        [](std::uint32_t slot) noexcept { return dwarf_number(slot); });
    out.u32(0); // floating point registers are not in this note
  }

private:
  static constexpr std::uint32_t register_slots = 17;
  static constexpr std::uint32_t no_register = detail::no_dwarf_register;

  // DWARF numbers in `user_regs_struct` order: ebx ecx edx esi edi ebp eax ds es fs gs orig_eax eip cs eflags esp ss.
  static constexpr std::uint32_t dwarf_number(std::uint32_t slot) noexcept {
    constexpr microfmt::array<std::uint32_t, register_slots> table{3,  1,  2,           6, 7,  5, 0, 43, 40,
                                                                   44, 45, no_register, 8, 41, 9, 4, 42};
    return slot < register_slots ? table[slot] : no_register;
  }
};

using arch_x86_32 = arch_x86_32_t<false>;
using arch_x86_32_fpu = arch_x86_32_t<true>;

} // namespace microfmt::coredump
