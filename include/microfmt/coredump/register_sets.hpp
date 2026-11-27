// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file register_sets.hpp
 * @brief Ready-made register structures a crashing program can fill in, plus their register-context mappings.
 *
 * Why: `elf_core_writer` reads a thread's registers through a `register_context_ref`. Writing that mapping by hand
 * means knowing the DWARF (debugging format) numbers of every register and the exact byte layout of the
 * floating-point save image. This header does it once per architecture: you fill a plain struct (from an
 * exception frame, a signal context or the CPU's save instructions) and wrap it in the matching `*_context`.
 *
 * How:
 * @code
 *   cd::x86_64_core_registers regs{};
 *   regs.gpr[cd::x86_64_core_registers::rip] = fault_pc;     // ... fill the rest
 *   std::byte scratch[16];
 *   cd::x86_64_core_register_context mapped(regs, space.ref(), scratch);
 *   thread.regs = mapped.ref();                              // keep `regs` and `mapped` alive while dumping
 * @endcode
 *
 * Each struct holds the general registers (read through their DWARF numbers, as `NT_PRSTATUS` needs) and the
 * floating-point image(s) as byte arrays in the exact layout of the matching note (read as one register-set blob,
 * see `core_regset`). Leave the image zeroed if you have no floating-point state.
 */

#include "../inspector/register_context.hpp"
#include "elf_format.hpp"

namespace microfmt::coredump {

/**
 * @brief Register field backed by an array member: DWARF registers `First` .. `First + Count - 1` map to elements 0 ..
 * Count - 1.
 */
template <auto Member, std::uint32_t First, std::uint32_t Count> struct core_register_array_field {
  using member_traits = microfmt::detail::register_member_pointer_traits<decltype(Member)>;
  using state_type = typename member_traits::state_type;
  using array_type = typename member_traits::value_type;
  using value_type = typename array_type::value_type;

  static_assert(Count != 0, "an array field must cover at least one register");
  static_assert(array_type{}.size() >= Count, "the array member must hold at least Count elements");

  [[nodiscard]] static constexpr bool matches(std::uint32_t index) noexcept {
    return index >= First && index - First < Count;
  }

  [[nodiscard]] static constexpr const value_type *get(const state_type &state, std::uint32_t index) noexcept {
    return matches(index) ? &(state.*Member)[index - First] : nullptr;
  }
};

namespace detail {

// A register-set image: the whole array is one register, fetched under `regset_index(NoteType)`.
template <auto Member, std::uint32_t NoteType>
using core_regset_field = register_member_field<Member, regset_index(NoteType)>;

} // namespace detail

/** @brief x86-64: `user_regs_struct` registers plus the 512-byte FXSAVE image (for `NT_PRFPREG`). */
struct x86_64_core_registers {
  /// Indexes into `gpr`, which follows the DWARF order rax rdx rcx rbx rsi rdi rbp rsp r8-r15 rip.
  enum gpr_index : std::uint32_t { rax, rdx, rcx, rbx, rsi, rdi, rbp, rsp, r8, r9, r10, r11, r12, r13, r14, r15, rip };
  /// Indexes into `segments`.
  enum segment_index : std::uint32_t { es, cs, ss, ds, fs, gs };

  microfmt::array<std::uint64_t, 17> gpr{};
  std::uint64_t eflags{0};
  microfmt::array<std::uint64_t, 6> segments{};
  std::uint64_t fs_base{0};
  std::uint64_t gs_base{0};
  microfmt::array<std::uint8_t, 512> fxsave{};
};

using x86_64_core_register_context =
    microfmt::register_context_ref_with<x86_64_core_registers,
                                        core_register_array_field<&x86_64_core_registers::gpr, 0, 17>,
                                        register_member_field<&x86_64_core_registers::eflags, 49>,
                                        core_register_array_field<&x86_64_core_registers::segments, 50, 6>,
                                        register_member_field<&x86_64_core_registers::fs_base, 58>,
                                        register_member_field<&x86_64_core_registers::gs_base, 59>,
                                        detail::core_regset_field<&x86_64_core_registers::fxsave, nt_prfpreg>>;

/** @brief x86 (32-bit): general registers plus the 108-byte legacy x87 image and the 512-byte FXSAVE image. */
struct x86_32_core_registers {
  /// Indexes into `gpr`, which follows the DWARF order eax ecx edx ebx esp ebp esi edi eip.
  enum gpr_index : std::uint32_t { eax, ecx, edx, ebx, esp, ebp, esi, edi, eip };
  enum segment_index : std::uint32_t { es, cs, ss, ds, fs, gs };

  microfmt::array<std::uint32_t, 9> gpr{};
  std::uint32_t eflags{0};
  microfmt::array<std::uint32_t, 6> segments{};
  microfmt::array<std::uint8_t, 108> fsave{};
  microfmt::array<std::uint8_t, 512> fxsave{};
};

using x86_32_core_register_context =
    microfmt::register_context_ref_with<x86_32_core_registers,
                                        core_register_array_field<&x86_32_core_registers::gpr, 0, 9>,
                                        register_member_field<&x86_32_core_registers::eflags, 9>,
                                        core_register_array_field<&x86_32_core_registers::segments, 40, 6>,
                                        detail::core_regset_field<&x86_32_core_registers::fsave, nt_prfpreg>,
                                        detail::core_regset_field<&x86_32_core_registers::fxsave, nt_prxfpreg>>;

/** @brief AArch64: x0-x30, sp, pc, pstate plus the 528-byte floating-point image (v0-v31, fpsr, fpcr). */
struct aarch64_core_registers {
  static constexpr std::uint32_t sp = 31;
  static constexpr std::uint32_t pc = 32;

  microfmt::array<std::uint64_t, 33> x{}; ///< x0-x30, then sp and pc
  std::uint64_t pstate{0};
  microfmt::array<std::uint8_t, 528> fpregs{};
};

using aarch64_core_register_context = microfmt::register_context_ref_with<
    aarch64_core_registers, core_register_array_field<&aarch64_core_registers::x, 0, 33>,
    register_member_field<&aarch64_core_registers::pstate, static_cast<std::uint32_t>(dwarf::aarch64::pstate)>,
    detail::core_regset_field<&aarch64_core_registers::fpregs, nt_prfpreg>>;

/** @brief RISC-V: x0-x31, pc and the 264-byte floating-point image (f0-f31 as doubles, fcsr). */
template <bool Is64> struct riscv_core_registers_t {
  using word_type = std::conditional_t<Is64, std::uint64_t, std::uint32_t>;

  microfmt::array<word_type, 32> x{};
  word_type pc{0};
  microfmt::array<std::uint8_t, 264> fpregs{};
};

template <bool Is64>
using riscv_core_register_context_t = microfmt::register_context_ref_with<
    riscv_core_registers_t<Is64>, core_register_array_field<&riscv_core_registers_t<Is64>::x, 0, 32>,
    register_member_field<&riscv_core_registers_t<Is64>::pc, static_cast<std::uint32_t>(dwarf::riscv::pc)>,
    detail::core_regset_field<&riscv_core_registers_t<Is64>::fpregs, nt_prfpreg>>;

using riscv32_core_registers = riscv_core_registers_t<false>;
using riscv64_core_registers = riscv_core_registers_t<true>;
using riscv32_core_register_context = riscv_core_register_context_t<false>;
using riscv64_core_register_context = riscv_core_register_context_t<true>;

/**
 * @brief 32-bit ARM: r0-r15, the program status register and the 260-byte VFP image (d0-d31, fpscr).
 * @tparam StatusRegister DWARF number of the status register: `xPSR` (Cortex-M) or `cpsr` (Cortex-A).
 */
template <std::uint32_t StatusRegister> struct arm_core_registers_t {
  microfmt::array<std::uint32_t, 16> r{};
  std::uint32_t status{0};
  microfmt::array<std::uint8_t, 260> vfp{};
};

template <std::uint32_t StatusRegister>
using arm_core_register_context_t = microfmt::register_context_ref_with<
    arm_core_registers_t<StatusRegister>, core_register_array_field<&arm_core_registers_t<StatusRegister>::r, 0, 16>,
    register_member_field<&arm_core_registers_t<StatusRegister>::status, StatusRegister>,
    detail::core_regset_field<&arm_core_registers_t<StatusRegister>::vfp, nt_arm_vfp>>;

using cortex_m_core_registers = arm_core_registers_t<static_cast<std::uint32_t>(dwarf::arm32::xpsr)>;
using cortex_a_core_registers = arm_core_registers_t<static_cast<std::uint32_t>(dwarf::arm32::cpsr)>;
using cortex_m_core_register_context = arm_core_register_context_t<static_cast<std::uint32_t>(dwarf::arm32::xpsr)>;
using cortex_a_core_register_context = arm_core_register_context_t<static_cast<std::uint32_t>(dwarf::arm32::cpsr)>;

} // namespace microfmt::coredump
