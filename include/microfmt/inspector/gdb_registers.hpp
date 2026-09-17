// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "../array.hpp"
#include "../span.hpp"
#include "../string_view.hpp"
#include "dwarf_registers.hpp"
#include <cstdint>

namespace microfmt::gdb {

/**
 * @brief Maps a single register between GDB and DWARF definitions.
 */
struct register_mapping {
  string_view name;
  uint32_t gdb_index;
  uint32_t dwarf_index;
  uint32_t bit_size;
  string_view gdb_type;
};

/**
 * @brief Shared lookup implementation for architecture-specific GDB register
 * traits.
 */
template <typename Traits> struct register_traits_base {
  [[nodiscard]] static constexpr const register_mapping *find_by_gdb(uint32_t index) noexcept {
    for (const auto &reg : Traits::arch_layout) {
      if (reg.gdb_index == index)
        return &reg;
    }
    for (const auto &reg : Traits::extended_arch_layout) {
      if (reg.gdb_index == index)
        return &reg;
    }
    for (const auto &reg : Traits::non_standard_arch_layout) {
      if (reg.gdb_index == index)
        return &reg;
    }
    return nullptr;
  }

  [[nodiscard]] static constexpr const register_mapping *find_by_dwarf(uint32_t index) noexcept {
    for (const auto &reg : Traits::arch_layout) {
      if (reg.dwarf_index == index)
        return &reg;
    }
    for (const auto &reg : Traits::extended_arch_layout) {
      if (reg.dwarf_index == index)
        return &reg;
    }
    for (const auto &reg : Traits::non_standard_arch_layout) {
      if (reg.dwarf_index == index)
        return &reg;
    }
    return nullptr;
  }

  [[nodiscard]] static constexpr const register_mapping *find_by_name(string_view name) noexcept {
    for (const auto &reg : Traits::arch_layout) {
      if (reg.name == name)
        return &reg;
    }
    for (const auto &reg : Traits::extended_arch_layout) {
      if (reg.name == name)
        return &reg;
    }
    for (const auto &reg : Traits::non_standard_arch_layout) {
      if (reg.name == name)
        return &reg;
    }
    return nullptr;
  }

  [[nodiscard]] static constexpr span<const register_mapping> layout() noexcept {
    return span<const register_mapping>{Traits::arch_layout.data(), Traits::arch_layout.size()};
  }

  /**
   * @brief Standard floating-point and vector register mappings.
   */
  [[nodiscard]] static constexpr span<const register_mapping> extended_layout() noexcept {
    return {Traits::extended_arch_layout.data(), Traits::extended_arch_layout.size()};
  }

  /**
   * @brief Target-specific system, control, debug, and privileged mappings.
   */
  [[nodiscard]] static constexpr span<const register_mapping> non_standard_layout() noexcept {
    return {Traits::non_standard_arch_layout.data(), Traits::non_standard_arch_layout.size()};
  }
};

/**
 * @brief Base template for architecture-specific GDB register traits.
 */
template <typename ArchTag> struct register_traits;

// ============================================================================
// Architecture Tags
// ============================================================================
namespace tags {
struct x86 {};
struct x86_64 {};
struct arm32 {};
struct aarch64 {};
struct riscv32 {};
struct riscv64 {};
} // namespace tags

// ============================================================================
// x86_64 (AMD64)
// ============================================================================
template <> struct register_traits<tags::x86_64> : register_traits_base<register_traits<tags::x86_64>> {
  static constexpr microfmt::array arch_layout{// GDB order vs DWARF order (Notice RCX/RDX/RBX swaps)
                                               register_mapping{"rax", 0, dwarf::x86_64::RAX, 64, "int64"},
                                               register_mapping{"rbx", 1, dwarf::x86_64::RBX, 64, "int64"},
                                               register_mapping{"rcx", 2, dwarf::x86_64::RCX, 64, "int64"},
                                               register_mapping{"rdx", 3, dwarf::x86_64::RDX, 64, "int64"},
                                               register_mapping{"rsi", 4, dwarf::x86_64::RSI, 64, "int64"},
                                               register_mapping{"rdi", 5, dwarf::x86_64::RDI, 64, "int64"},
                                               register_mapping{"rbp", 6, dwarf::x86_64::RBP, 64, "data_ptr"},
                                               register_mapping{"rsp", 7, dwarf::x86_64::RSP, 64, "data_ptr"},
                                               register_mapping{"r8", 8, dwarf::x86_64::R8, 64, "int64"},
                                               register_mapping{"r9", 9, dwarf::x86_64::R9, 64, "int64"},
                                               register_mapping{"r10", 10, dwarf::x86_64::R10, 64, "int64"},
                                               register_mapping{"r11", 11, dwarf::x86_64::R11, 64, "int64"},
                                               register_mapping{"r12", 12, dwarf::x86_64::R12, 64, "int64"},
                                               register_mapping{"r13", 13, dwarf::x86_64::R13, 64, "int64"},
                                               register_mapping{"r14", 14, dwarf::x86_64::R14, 64, "int64"},
                                               register_mapping{"r15", 15, dwarf::x86_64::R15, 64, "int64"},
                                               register_mapping{"rip", 16, dwarf::x86_64::RIP, 64, "code_ptr"},
                                               register_mapping{"eflags", 17, dwarf::x86_64::EFLAGS, 32, "int32"},
                                               register_mapping{"cs", 18, dwarf::x86_64::CS, 32, "int32"},
                                               register_mapping{"ss", 19, dwarf::x86_64::SS, 32, "int32"},
                                               register_mapping{"ds", 20, dwarf::x86_64::DS, 32, "int32"},
                                               register_mapping{"es", 21, dwarf::x86_64::ES, 32, "int32"},
                                               register_mapping{"fs", 22, dwarf::x86_64::FS, 32, "int32"},
                                               register_mapping{"gs", 23, dwarf::x86_64::GS, 32, "int32"}};

  static constexpr microfmt::array extended_arch_layout{register_mapping{"xmm0", 40, dwarf::x86_64::XMM0, 128, ""},
                                                        register_mapping{"xmm1", 41, dwarf::x86_64::XMM1, 128, ""},
                                                        register_mapping{"xmm2", 42, dwarf::x86_64::XMM2, 128, ""},
                                                        register_mapping{"xmm3", 43, dwarf::x86_64::XMM3, 128, ""},
                                                        register_mapping{"xmm4", 44, dwarf::x86_64::XMM4, 128, ""},
                                                        register_mapping{"xmm5", 45, dwarf::x86_64::XMM5, 128, ""},
                                                        register_mapping{"xmm6", 46, dwarf::x86_64::XMM6, 128, ""},
                                                        register_mapping{"xmm7", 47, dwarf::x86_64::XMM7, 128, ""},
                                                        register_mapping{"xmm8", 48, dwarf::x86_64::XMM8, 128, ""},
                                                        register_mapping{"xmm9", 49, dwarf::x86_64::XMM9, 128, ""},
                                                        register_mapping{"xmm10", 50, dwarf::x86_64::XMM10, 128, ""},
                                                        register_mapping{"xmm11", 51, dwarf::x86_64::XMM11, 128, ""},
                                                        register_mapping{"xmm12", 52, dwarf::x86_64::XMM12, 128, ""},
                                                        register_mapping{"xmm13", 53, dwarf::x86_64::XMM13, 128, ""},
                                                        register_mapping{"xmm14", 54, dwarf::x86_64::XMM14, 128, ""},
                                                        register_mapping{"xmm15", 55, dwarf::x86_64::XMM15, 128, ""}};

  static constexpr microfmt::array non_standard_arch_layout{
      register_mapping{"fs_base", 58, dwarf::x86_64::FS_BASE, 64, "data_ptr"},
      register_mapping{"gs_base", 59, dwarf::x86_64::GS_BASE, 64, "data_ptr"},
      register_mapping{"kernel_gs_base", 66, dwarf::x86_64::KERNEL_GS_BASE, 64, "data_ptr"},
      register_mapping{"cr0", 100, dwarf::x86_64::CR0, 64, "int64"},
      register_mapping{"cr2", 102, dwarf::x86_64::CR2, 64, "data_ptr"},
      register_mapping{"cr3", 103, dwarf::x86_64::CR3, 64, "data_ptr"},
      register_mapping{"cr4", 104, dwarf::x86_64::CR4, 64, "int64"},
      register_mapping{"cr8", 108, dwarf::x86_64::CR8, 64, "int64"},
      register_mapping{"dr0", 120, dwarf::x86_64::DR0, 64, "data_ptr"},
      register_mapping{"dr1", 121, dwarf::x86_64::DR1, 64, "data_ptr"},
      register_mapping{"dr2", 122, dwarf::x86_64::DR2, 64, "data_ptr"},
      register_mapping{"dr3", 123, dwarf::x86_64::DR3, 64, "data_ptr"},
      register_mapping{"dr6", 126, dwarf::x86_64::DR6, 64, "int64"},
      register_mapping{"dr7", 127, dwarf::x86_64::DR7, 64, "int64"}};
};

// ============================================================================
// x86 (IA-32)
// ============================================================================
template <> struct register_traits<tags::x86> : register_traits_base<register_traits<tags::x86>> {
  static constexpr microfmt::array arch_layout{// x86 GDB and DWARF match exactly for the first 9 registers
                                               register_mapping{"eax", 0, dwarf::x86::EAX, 32, "int32"},
                                               register_mapping{"ecx", 1, dwarf::x86::ECX, 32, "int32"},
                                               register_mapping{"edx", 2, dwarf::x86::EDX, 32, "int32"},
                                               register_mapping{"ebx", 3, dwarf::x86::EBX, 32, "int32"},
                                               register_mapping{"esp", 4, dwarf::x86::ESP, 32, "data_ptr"},
                                               register_mapping{"ebp", 5, dwarf::x86::EBP, 32, "data_ptr"},
                                               register_mapping{"esi", 6, dwarf::x86::ESI, 32, "int32"},
                                               register_mapping{"edi", 7, dwarf::x86::EDI, 32, "int32"},
                                               register_mapping{"eip", 8, dwarf::x86::EIP, 32, "code_ptr"}};

  static constexpr microfmt::array extended_arch_layout{register_mapping{"st0", 16, dwarf::x86::ST0, 80, "i387_ext"},
                                                        register_mapping{"st1", 17, dwarf::x86::ST1, 80, "i387_ext"},
                                                        register_mapping{"st2", 18, dwarf::x86::ST2, 80, "i387_ext"},
                                                        register_mapping{"st3", 19, dwarf::x86::ST3, 80, "i387_ext"},
                                                        register_mapping{"st4", 20, dwarf::x86::ST4, 80, "i387_ext"},
                                                        register_mapping{"st5", 21, dwarf::x86::ST5, 80, "i387_ext"},
                                                        register_mapping{"st6", 22, dwarf::x86::ST6, 80, "i387_ext"},
                                                        register_mapping{"st7", 23, dwarf::x86::ST7, 80, "i387_ext"},
                                                        register_mapping{"xmm0", 32, dwarf::x86::XMM0, 128, ""},
                                                        register_mapping{"xmm1", 33, dwarf::x86::XMM1, 128, ""},
                                                        register_mapping{"xmm2", 34, dwarf::x86::XMM2, 128, ""},
                                                        register_mapping{"xmm3", 35, dwarf::x86::XMM3, 128, ""},
                                                        register_mapping{"xmm4", 36, dwarf::x86::XMM4, 128, ""},
                                                        register_mapping{"xmm5", 37, dwarf::x86::XMM5, 128, ""},
                                                        register_mapping{"xmm6", 38, dwarf::x86::XMM6, 128, ""},
                                                        register_mapping{"xmm7", 39, dwarf::x86::XMM7, 128, ""}};

  static constexpr microfmt::array non_standard_arch_layout{
      register_mapping{"gs_base", 58, dwarf::x86::GS_BASE, 32, "data_ptr"},
      register_mapping{"fs_base", 59, dwarf::x86::FS_BASE, 32, "data_ptr"},
      register_mapping{"cr0", 100, dwarf::x86::CR0, 32, "int32"},
      register_mapping{"cr3", 103, dwarf::x86::CR3, 32, "data_ptr"},
      register_mapping{"cr4", 104, dwarf::x86::CR4, 32, "int32"}};
};

// ============================================================================
// AArch64 (ARM 64-bit)
// ============================================================================
template <> struct register_traits<tags::aarch64> : register_traits_base<register_traits<tags::aarch64>> {
  static constexpr microfmt::array arch_layout{
      register_mapping{"x0", 0, dwarf::aarch64::X0, 64, "int64"},
      register_mapping{"x1", 1, dwarf::aarch64::X1, 64, "int64"},
      register_mapping{"x2", 2, dwarf::aarch64::X2, 64, "int64"},
      register_mapping{"x3", 3, dwarf::aarch64::X3, 64, "int64"},
      register_mapping{"x4", 4, dwarf::aarch64::X4, 64, "int64"},
      register_mapping{"x5", 5, dwarf::aarch64::X5, 64, "int64"},
      register_mapping{"x6", 6, dwarf::aarch64::X6, 64, "int64"},
      register_mapping{"x7", 7, dwarf::aarch64::X7, 64, "int64"},
      register_mapping{"x8", 8, dwarf::aarch64::X8, 64, "int64"},
      register_mapping{"x9", 9, dwarf::aarch64::X9, 64, "int64"},
      register_mapping{"x10", 10, dwarf::aarch64::X10, 64, "int64"},
      register_mapping{"x11", 11, dwarf::aarch64::X11, 64, "int64"},
      register_mapping{"x12", 12, dwarf::aarch64::X12, 64, "int64"},
      register_mapping{"x13", 13, dwarf::aarch64::X13, 64, "int64"},
      register_mapping{"x14", 14, dwarf::aarch64::X14, 64, "int64"},
      register_mapping{"x15", 15, dwarf::aarch64::X15, 64, "int64"},
      register_mapping{"x16", 16, dwarf::aarch64::X16, 64, "int64"},
      register_mapping{"x17", 17, dwarf::aarch64::X17, 64, "int64"},
      register_mapping{"x18", 18, dwarf::aarch64::X18, 64, "int64"},
      register_mapping{"x19", 19, dwarf::aarch64::X19, 64, "int64"},
      register_mapping{"x20", 20, dwarf::aarch64::X20, 64, "int64"},
      register_mapping{"x21", 21, dwarf::aarch64::X21, 64, "int64"},
      register_mapping{"x22", 22, dwarf::aarch64::X22, 64, "int64"},
      register_mapping{"x23", 23, dwarf::aarch64::X23, 64, "int64"},
      register_mapping{"x24", 24, dwarf::aarch64::X24, 64, "int64"},
      register_mapping{"x25", 25, dwarf::aarch64::X25, 64, "int64"},
      register_mapping{"x26", 26, dwarf::aarch64::X26, 64, "int64"},
      register_mapping{"x27", 27, dwarf::aarch64::X27, 64, "int64"},
      register_mapping{"x28", 28, dwarf::aarch64::X28, 64, "int64"},
      register_mapping{"x29", 29, dwarf::aarch64::FP, 64, "data_ptr"},
      register_mapping{"x30", 30, dwarf::aarch64::LR, 64, "code_ptr"},
      register_mapping{"sp", 31, dwarf::aarch64::SP, 64, "data_ptr"},
      register_mapping{"pc", 32, dwarf::aarch64::PC, 64, "code_ptr"},
      register_mapping{"cpsr", 33, dwarf::aarch64::PSTATE, 32, "int32"} // PSTATE sent as 32-bit CPSR equivalent
  };

  static constexpr microfmt::array extended_arch_layout{register_mapping{"v0", 34, dwarf::aarch64::V0, 128, ""},
                                                        register_mapping{"v1", 35, dwarf::aarch64::V1, 128, ""},
                                                        register_mapping{"v2", 36, dwarf::aarch64::V2, 128, ""},
                                                        register_mapping{"v3", 37, dwarf::aarch64::V3, 128, ""},
                                                        register_mapping{"v4", 38, dwarf::aarch64::V4, 128, ""},
                                                        register_mapping{"v5", 39, dwarf::aarch64::V5, 128, ""},
                                                        register_mapping{"v6", 40, dwarf::aarch64::V6, 128, ""},
                                                        register_mapping{"v7", 41, dwarf::aarch64::V7, 128, ""},
                                                        register_mapping{"v8", 42, dwarf::aarch64::V8, 128, ""},
                                                        register_mapping{"v9", 43, dwarf::aarch64::V9, 128, ""},
                                                        register_mapping{"v10", 44, dwarf::aarch64::V10, 128, ""},
                                                        register_mapping{"v11", 45, dwarf::aarch64::V11, 128, ""},
                                                        register_mapping{"v12", 46, dwarf::aarch64::V12, 128, ""},
                                                        register_mapping{"v13", 47, dwarf::aarch64::V13, 128, ""},
                                                        register_mapping{"v14", 48, dwarf::aarch64::V14, 128, ""},
                                                        register_mapping{"v15", 49, dwarf::aarch64::V15, 128, ""},
                                                        register_mapping{"v16", 50, dwarf::aarch64::V16, 128, ""},
                                                        register_mapping{"v17", 51, dwarf::aarch64::V17, 128, ""},
                                                        register_mapping{"v18", 52, dwarf::aarch64::V18, 128, ""},
                                                        register_mapping{"v19", 53, dwarf::aarch64::V19, 128, ""},
                                                        register_mapping{"v20", 54, dwarf::aarch64::V20, 128, ""},
                                                        register_mapping{"v21", 55, dwarf::aarch64::V21, 128, ""},
                                                        register_mapping{"v22", 56, dwarf::aarch64::V22, 128, ""},
                                                        register_mapping{"v23", 57, dwarf::aarch64::V23, 128, ""},
                                                        register_mapping{"v24", 58, dwarf::aarch64::V24, 128, ""},
                                                        register_mapping{"v25", 59, dwarf::aarch64::V25, 128, ""},
                                                        register_mapping{"v26", 60, dwarf::aarch64::V26, 128, ""},
                                                        register_mapping{"v27", 61, dwarf::aarch64::V27, 128, ""},
                                                        register_mapping{"v28", 62, dwarf::aarch64::V28, 128, ""},
                                                        register_mapping{"v29", 63, dwarf::aarch64::V29, 128, ""},
                                                        register_mapping{"v30", 64, dwarf::aarch64::V30, 128, ""},
                                                        register_mapping{"v31", 65, dwarf::aarch64::V31, 128, ""}};

  static constexpr microfmt::array non_standard_arch_layout{
      register_mapping{"tpidr_el0", dwarf::aarch64::TPIDR_EL0, dwarf::aarch64::TPIDR_EL0, 64, "data_ptr"},
      register_mapping{"tpidrro_el0", dwarf::aarch64::TPIDRRO_EL0, dwarf::aarch64::TPIDRRO_EL0, 64, "data_ptr"},
      register_mapping{"tpidr_el1", dwarf::aarch64::TPIDR_EL1, dwarf::aarch64::TPIDR_EL1, 64, "data_ptr"},
      register_mapping{"tpidr_el2", dwarf::aarch64::TPIDR_EL2, dwarf::aarch64::TPIDR_EL2, 64, "data_ptr"},
      register_mapping{"tpidr_el3", dwarf::aarch64::TPIDR_EL3, dwarf::aarch64::TPIDR_EL3, 64, "data_ptr"},
      register_mapping{"sp_el0", dwarf::aarch64::SP_EL0, dwarf::aarch64::SP_EL0, 64, "data_ptr"},
      register_mapping{"sp_el1", dwarf::aarch64::SP_EL1, dwarf::aarch64::SP_EL1, 64, "data_ptr"},
      register_mapping{"elr_el1", dwarf::aarch64::ELR_EL1, dwarf::aarch64::ELR_EL1, 64, "code_ptr"},
      register_mapping{"spsr_el1", dwarf::aarch64::SPSR_EL1, dwarf::aarch64::SPSR_EL1, 64, "int64"},
      register_mapping{"sctlr_el1", dwarf::aarch64::SCTLR_EL1, dwarf::aarch64::SCTLR_EL1, 64, "int64"},
      register_mapping{"vbar_el1", dwarf::aarch64::VBAR_EL1, dwarf::aarch64::VBAR_EL1, 64, "code_ptr"},
      register_mapping{"vbar_el2", dwarf::aarch64::VBAR_EL2, dwarf::aarch64::VBAR_EL2, 64, "code_ptr"},
      register_mapping{"sp_el2", dwarf::aarch64::SP_EL2, dwarf::aarch64::SP_EL2, 64, "data_ptr"},
      register_mapping{"sp_el3", dwarf::aarch64::SP_EL3, dwarf::aarch64::SP_EL3, 64, "data_ptr"},
      register_mapping{"elr_el2", dwarf::aarch64::ELR_EL2, dwarf::aarch64::ELR_EL2, 64, "code_ptr"},
      register_mapping{"elr_el3", dwarf::aarch64::ELR_EL3, dwarf::aarch64::ELR_EL3, 64, "code_ptr"},
      register_mapping{"spsr_el2", dwarf::aarch64::SPSR_EL2, dwarf::aarch64::SPSR_EL2, 64, "int64"},
      register_mapping{"spsr_el3", dwarf::aarch64::SPSR_EL3, dwarf::aarch64::SPSR_EL3, 64, "int64"},
      register_mapping{"sctlr_el2", dwarf::aarch64::SCTLR_EL2, dwarf::aarch64::SCTLR_EL2, 64, "int64"},
      register_mapping{"sctlr_el3", dwarf::aarch64::SCTLR_EL3, dwarf::aarch64::SCTLR_EL3, 64, "int64"},
      register_mapping{"vbar_el3", dwarf::aarch64::VBAR_EL3, dwarf::aarch64::VBAR_EL3, 64, "code_ptr"},
      register_mapping{"ttbr0_el1", dwarf::aarch64::TTBR0_EL1, dwarf::aarch64::TTBR0_EL1, 64, "data_ptr"},
      register_mapping{"ttbr1_el1", dwarf::aarch64::TTBR1_EL1, dwarf::aarch64::TTBR1_EL1, 64, "data_ptr"},
      register_mapping{"tcr_el1", dwarf::aarch64::TCR_EL1, dwarf::aarch64::TCR_EL1, 64, "int64"},
      register_mapping{"mair_el1", dwarf::aarch64::MAIR_EL1, dwarf::aarch64::MAIR_EL1, 64, "int64"},
      register_mapping{"amair_el1", dwarf::aarch64::AMAIR_EL1, dwarf::aarch64::AMAIR_EL1, 64, "int64"},
      register_mapping{"esr_el1", dwarf::aarch64::ESR_EL1, dwarf::aarch64::ESR_EL1, 64, "int64"},
      register_mapping{"far_el1", dwarf::aarch64::FAR_EL1, dwarf::aarch64::FAR_EL1, 64, "data_ptr"},
      register_mapping{"par_el1", dwarf::aarch64::PAR_EL1, dwarf::aarch64::PAR_EL1, 64, "int64"},
      register_mapping{"contextidr_el1", dwarf::aarch64::CONTEXTIDR_EL1, dwarf::aarch64::CONTEXTIDR_EL1, 64, "int64"},
      register_mapping{"cpacr_el1", dwarf::aarch64::CPACR_EL1, dwarf::aarch64::CPACR_EL1, 64, "int64"},
      register_mapping{"midr_el1", dwarf::aarch64::MIDR_EL1, dwarf::aarch64::MIDR_EL1, 64, "int64"},
      register_mapping{"mpidr_el1", dwarf::aarch64::MPIDR_EL1, dwarf::aarch64::MPIDR_EL1, 64, "int64"},
      register_mapping{"revidr_el1", dwarf::aarch64::REVIDR_EL1, dwarf::aarch64::REVIDR_EL1, 64, "int64"},
      register_mapping{"id_aa64pfr0_el1", dwarf::aarch64::ID_AA64PFR0_EL1, dwarf::aarch64::ID_AA64PFR0_EL1, 64,
                       "int64"},
      register_mapping{"id_aa64mmfr0_el1", dwarf::aarch64::ID_AA64MMFR0_EL1, dwarf::aarch64::ID_AA64MMFR0_EL1, 64,
                       "int64"},
      register_mapping{"id_aa64isar0_el1", dwarf::aarch64::ID_AA64ISAR0_EL1, dwarf::aarch64::ID_AA64ISAR0_EL1, 64,
                       "int64"},
      register_mapping{"ttbr0_el2", dwarf::aarch64::TTBR0_EL2, dwarf::aarch64::TTBR0_EL2, 64, "data_ptr"},
      register_mapping{"tcr_el2", dwarf::aarch64::TCR_EL2, dwarf::aarch64::TCR_EL2, 64, "int64"},
      register_mapping{"mair_el2", dwarf::aarch64::MAIR_EL2, dwarf::aarch64::MAIR_EL2, 64, "int64"},
      register_mapping{"esr_el2", dwarf::aarch64::ESR_EL2, dwarf::aarch64::ESR_EL2, 64, "int64"},
      register_mapping{"far_el2", dwarf::aarch64::FAR_EL2, dwarf::aarch64::FAR_EL2, 64, "data_ptr"},
      register_mapping{"hcr_el2", dwarf::aarch64::HCR_EL2, dwarf::aarch64::HCR_EL2, 64, "int64"},
      register_mapping{"vtcr_el2", dwarf::aarch64::VTCR_EL2, dwarf::aarch64::VTCR_EL2, 64, "int64"},
      register_mapping{"vttbr_el2", dwarf::aarch64::VTTBR_EL2, dwarf::aarch64::VTTBR_EL2, 64, "data_ptr"},
      register_mapping{"scr_el3", dwarf::aarch64::SCR_EL3, dwarf::aarch64::SCR_EL3, 64, "int64"},
      register_mapping{"esr_el3", dwarf::aarch64::ESR_EL3, dwarf::aarch64::ESR_EL3, 64, "int64"},
      register_mapping{"far_el3", dwarf::aarch64::FAR_EL3, dwarf::aarch64::FAR_EL3, 64, "data_ptr"},
      register_mapping{"currentel", dwarf::aarch64::CURRENTEL, dwarf::aarch64::CURRENTEL, 64, "int64"},
      register_mapping{"daif", dwarf::aarch64::DAIF, dwarf::aarch64::DAIF, 64, "int64"},
      register_mapping{"nzcv", dwarf::aarch64::NZCV, dwarf::aarch64::NZCV, 64, "int64"},
      register_mapping{"cntfrq_el0", dwarf::aarch64::CNTFRQ_EL0, dwarf::aarch64::CNTFRQ_EL0, 32, "int32"},
      register_mapping{"cntpct_el0", dwarf::aarch64::CNTPCT_EL0, dwarf::aarch64::CNTPCT_EL0, 64, "int64"},
      register_mapping{"cntvct_el0", dwarf::aarch64::CNTVCT_EL0, dwarf::aarch64::CNTVCT_EL0, 64, "int64"},
      register_mapping{"cntp_tval_el0", dwarf::aarch64::CNTP_TVAL_EL0, dwarf::aarch64::CNTP_TVAL_EL0, 32, "int32"},
      register_mapping{"cntp_ctl_el0", dwarf::aarch64::CNTP_CTL_EL0, dwarf::aarch64::CNTP_CTL_EL0, 32, "int32"},
      register_mapping{"cntp_cval_el0", dwarf::aarch64::CNTP_CVAL_EL0, dwarf::aarch64::CNTP_CVAL_EL0, 64, "int64"},
      register_mapping{"cntv_tval_el0", dwarf::aarch64::CNTV_TVAL_EL0, dwarf::aarch64::CNTV_TVAL_EL0, 32, "int32"},
      register_mapping{"cntv_ctl_el0", dwarf::aarch64::CNTV_CTL_EL0, dwarf::aarch64::CNTV_CTL_EL0, 32, "int32"},
      register_mapping{"cntv_cval_el0", dwarf::aarch64::CNTV_CVAL_EL0, dwarf::aarch64::CNTV_CVAL_EL0, 64, "int64"},
      register_mapping{"cnthp_tval_el2", dwarf::aarch64::CNTHP_TVAL_EL2, dwarf::aarch64::CNTHP_TVAL_EL2, 32, "int32"},
      register_mapping{"cnthp_ctl_el2", dwarf::aarch64::CNTHP_CTL_EL2, dwarf::aarch64::CNTHP_CTL_EL2, 32, "int32"},
      register_mapping{"cnthp_cval_el2", dwarf::aarch64::CNTHP_CVAL_EL2, dwarf::aarch64::CNTHP_CVAL_EL2, 64, "int64"},
      register_mapping{"cntvoff_el2", dwarf::aarch64::CNTVOFF_EL2, dwarf::aarch64::CNTVOFF_EL2, 64, "int64"},
      register_mapping{"cnthctl_el2", dwarf::aarch64::CNTHCTL_EL2, dwarf::aarch64::CNTHCTL_EL2, 32, "int32"},
      register_mapping{"cnthv_tval_el2", dwarf::aarch64::CNTHV_TVAL_EL2, dwarf::aarch64::CNTHV_TVAL_EL2, 32, "int32"},
      register_mapping{"cnthv_ctl_el2", dwarf::aarch64::CNTHV_CTL_EL2, dwarf::aarch64::CNTHV_CTL_EL2, 32, "int32"},
      register_mapping{"cnthv_cval_el2", dwarf::aarch64::CNTHV_CVAL_EL2, dwarf::aarch64::CNTHV_CVAL_EL2, 64, "int64"},
      register_mapping{"cnthps_tval_el2", dwarf::aarch64::CNTHPS_TVAL_EL2, dwarf::aarch64::CNTHPS_TVAL_EL2, 32,
                       "int32"},
      register_mapping{"cnthps_ctl_el2", dwarf::aarch64::CNTHPS_CTL_EL2, dwarf::aarch64::CNTHPS_CTL_EL2, 32, "int32"},
      register_mapping{"cnthps_cval_el2", dwarf::aarch64::CNTHPS_CVAL_EL2, dwarf::aarch64::CNTHPS_CVAL_EL2, 64,
                       "int64"},
      register_mapping{"cnthvs_tval_el2", dwarf::aarch64::CNTHVS_TVAL_EL2, dwarf::aarch64::CNTHVS_TVAL_EL2, 32,
                       "int32"},
      register_mapping{"cnthvs_ctl_el2", dwarf::aarch64::CNTHVS_CTL_EL2, dwarf::aarch64::CNTHVS_CTL_EL2, 32, "int32"},
      register_mapping{"cnthvs_cval_el2", dwarf::aarch64::CNTHVS_CVAL_EL2, dwarf::aarch64::CNTHVS_CVAL_EL2, 64,
                       "int64"},
      register_mapping{"cntkctl_el1", dwarf::aarch64::CNTKCTL_EL1, dwarf::aarch64::CNTKCTL_EL1, 32, "int32"}};
};

// ============================================================================
// ARM32 (AArch32)
// ============================================================================
template <> struct register_traits<tags::arm32> : register_traits_base<register_traits<tags::arm32>> {
  static constexpr microfmt::array arch_layout{
      register_mapping{"r0", 0, dwarf::arm32::R0, 32, "int32"},
      register_mapping{"r1", 1, dwarf::arm32::R1, 32, "int32"},
      register_mapping{"r2", 2, dwarf::arm32::R2, 32, "int32"},
      register_mapping{"r3", 3, dwarf::arm32::R3, 32, "int32"},
      register_mapping{"r4", 4, dwarf::arm32::R4, 32, "int32"},
      register_mapping{"r5", 5, dwarf::arm32::R5, 32, "int32"},
      register_mapping{"r6", 6, dwarf::arm32::R6, 32, "int32"},
      register_mapping{"r7", 7, dwarf::arm32::R7, 32, "int32"},
      register_mapping{"r8", 8, dwarf::arm32::R8, 32, "int32"},
      register_mapping{"r9", 9, dwarf::arm32::R9, 32, "int32"},
      register_mapping{"r10", 10, dwarf::arm32::R10, 32, "int32"},
      register_mapping{"r11", 11, dwarf::arm32::FP, 32, "data_ptr"},
      register_mapping{"r12", 12, dwarf::arm32::R12, 32, "int32"},
      register_mapping{"sp", 13, dwarf::arm32::SP, 32, "data_ptr"},
      register_mapping{"lr", 14, dwarf::arm32::LR, 32, "code_ptr"},
      register_mapping{"pc", 15, dwarf::arm32::PC, 32, "code_ptr"},

      // Note: GDB ARM layouts typically map FPA floats at 16-24. CPSR is commonly 25.
      register_mapping{"cpsr", 25, dwarf::arm32::CPSR, 32, "int32"}};

  static constexpr microfmt::array extended_arch_layout{
      register_mapping{"d0", 26, dwarf::arm32::D0, 64, "ieee_double"},
      register_mapping{"d1", 27, dwarf::arm32::D1, 64, "ieee_double"},
      register_mapping{"d2", 28, dwarf::arm32::D2, 64, "ieee_double"},
      register_mapping{"d3", 29, dwarf::arm32::D3, 64, "ieee_double"},
      register_mapping{"d4", 30, dwarf::arm32::D4, 64, "ieee_double"},
      register_mapping{"d5", 31, dwarf::arm32::D5, 64, "ieee_double"},
      register_mapping{"d6", 32, dwarf::arm32::D6, 64, "ieee_double"},
      register_mapping{"d7", 33, dwarf::arm32::D7, 64, "ieee_double"},
      register_mapping{"d8", 34, dwarf::arm32::D8, 64, "ieee_double"},
      register_mapping{"d9", 35, dwarf::arm32::D9, 64, "ieee_double"},
      register_mapping{"d10", 36, dwarf::arm32::D10, 64, "ieee_double"},
      register_mapping{"d11", 37, dwarf::arm32::D11, 64, "ieee_double"},
      register_mapping{"d12", 38, dwarf::arm32::D12, 64, "ieee_double"},
      register_mapping{"d13", 39, dwarf::arm32::D13, 64, "ieee_double"},
      register_mapping{"d14", 40, dwarf::arm32::D14, 64, "ieee_double"},
      register_mapping{"d15", 41, dwarf::arm32::D15, 64, "ieee_double"},
      register_mapping{"d16", 42, dwarf::arm32::D16, 64, "ieee_double"},
      register_mapping{"d17", 43, dwarf::arm32::D17, 64, "ieee_double"},
      register_mapping{"d18", 44, dwarf::arm32::D18, 64, "ieee_double"},
      register_mapping{"d19", 45, dwarf::arm32::D19, 64, "ieee_double"},
      register_mapping{"d20", 46, dwarf::arm32::D20, 64, "ieee_double"},
      register_mapping{"d21", 47, dwarf::arm32::D21, 64, "ieee_double"},
      register_mapping{"d22", 48, dwarf::arm32::D22, 64, "ieee_double"},
      register_mapping{"d23", 49, dwarf::arm32::D23, 64, "ieee_double"},
      register_mapping{"d24", 50, dwarf::arm32::D24, 64, "ieee_double"},
      register_mapping{"d25", 51, dwarf::arm32::D25, 64, "ieee_double"},
      register_mapping{"d26", 52, dwarf::arm32::D26, 64, "ieee_double"},
      register_mapping{"d27", 53, dwarf::arm32::D27, 64, "ieee_double"},
      register_mapping{"d28", 54, dwarf::arm32::D28, 64, "ieee_double"},
      register_mapping{"d29", 55, dwarf::arm32::D29, 64, "ieee_double"},
      register_mapping{"d30", 56, dwarf::arm32::D30, 64, "ieee_double"},
      register_mapping{"d31", 57, dwarf::arm32::D31, 64, "ieee_double"}};

  static constexpr microfmt::array non_standard_arch_layout{
      register_mapping{"tpidrurw", dwarf::arm32::TPIDRURW, dwarf::arm32::TPIDRURW, 32, "data_ptr"},
      register_mapping{"tpidruro", dwarf::arm32::TPIDRURO, dwarf::arm32::TPIDRURO, 32, "data_ptr"},
      register_mapping{"tpidrprw", dwarf::arm32::TPIDRPRW, dwarf::arm32::TPIDRPRW, 32, "data_ptr"},
      register_mapping{"spsr", dwarf::arm32::SPSR, dwarf::arm32::SPSR, 32, "int32"},
      register_mapping{"apsr", dwarf::arm32::APSR, dwarf::arm32::APSR, 32, "int32"},
      register_mapping{"iapsr", dwarf::arm32::IAPSR, dwarf::arm32::IAPSR, 32, "int32"},
      register_mapping{"eapsr", dwarf::arm32::EAPSR, dwarf::arm32::EAPSR, 32, "int32"},
      register_mapping{"xpsr", dwarf::arm32::XPSR, dwarf::arm32::XPSR, 32, "int32"},
      register_mapping{"ipsr", dwarf::arm32::IPSR, dwarf::arm32::IPSR, 32, "int32"},
      register_mapping{"epsr", dwarf::arm32::EPSR, dwarf::arm32::EPSR, 32, "int32"},
      register_mapping{"iepsr", dwarf::arm32::IEPSR, dwarf::arm32::IEPSR, 32, "int32"},
      register_mapping{"msp", dwarf::arm32::MSP, dwarf::arm32::MSP, 32, "data_ptr"},
      register_mapping{"psp", dwarf::arm32::PSP, dwarf::arm32::PSP, 32, "data_ptr"},
      register_mapping{"primask", dwarf::arm32::PRIMASK, dwarf::arm32::PRIMASK, 32, "int32"},
      register_mapping{"basepri", dwarf::arm32::BASEPRI, dwarf::arm32::BASEPRI, 32, "int32"},
      register_mapping{"basepri_max", dwarf::arm32::BASEPRI_MAX, dwarf::arm32::BASEPRI_MAX, 32, "int32"},
      register_mapping{"faultmask", dwarf::arm32::FAULTMASK, dwarf::arm32::FAULTMASK, 32, "int32"},
      register_mapping{"control", dwarf::arm32::CONTROL, dwarf::arm32::CONTROL, 32, "int32"},
      register_mapping{"sctlr", dwarf::arm32::SCTLR, dwarf::arm32::SCTLR, 32, "int32"},
      register_mapping{"actlr", dwarf::arm32::ACTLR, dwarf::arm32::ACTLR, 32, "int32"},
      register_mapping{"cpacr", dwarf::arm32::CPACR, dwarf::arm32::CPACR, 32, "int32"},
      register_mapping{"ttbr0", dwarf::arm32::TTBR0, dwarf::arm32::TTBR0, 32, "data_ptr"},
      register_mapping{"ttbr1", dwarf::arm32::TTBR1, dwarf::arm32::TTBR1, 32, "data_ptr"},
      register_mapping{"ttbcr", dwarf::arm32::TTBCR, dwarf::arm32::TTBCR, 32, "int32"},
      register_mapping{"dacr", dwarf::arm32::DACR, dwarf::arm32::DACR, 32, "int32"},
      register_mapping{"dfsr", dwarf::arm32::DFSR, dwarf::arm32::DFSR, 32, "int32"},
      register_mapping{"ifsr", dwarf::arm32::IFSR, dwarf::arm32::IFSR, 32, "int32"},
      register_mapping{"dfar", dwarf::arm32::DFAR, dwarf::arm32::DFAR, 32, "data_ptr"},
      register_mapping{"ifar", dwarf::arm32::IFAR, dwarf::arm32::IFAR, 32, "data_ptr"},
      register_mapping{"vbar", dwarf::arm32::VBAR, dwarf::arm32::VBAR, 32, "code_ptr"},
      register_mapping{"contextidr", dwarf::arm32::CONTEXTIDR, dwarf::arm32::CONTEXTIDR, 32, "int32"},
      register_mapping{"mair0", dwarf::arm32::MAIR0, dwarf::arm32::MAIR0, 32, "int32"},
      register_mapping{"mair1", dwarf::arm32::MAIR1, dwarf::arm32::MAIR1, 32, "int32"},
      register_mapping{"amair0", dwarf::arm32::AMAIR0, dwarf::arm32::AMAIR0, 32, "int32"},
      register_mapping{"amair1", dwarf::arm32::AMAIR1, dwarf::arm32::AMAIR1, 32, "int32"},
      register_mapping{"midr", dwarf::arm32::MIDR, dwarf::arm32::MIDR, 32, "int32"},
      register_mapping{"mpidr", dwarf::arm32::MPIDR, dwarf::arm32::MPIDR, 32, "int32"},
      register_mapping{"cntfrq", dwarf::arm32::CNTFRQ, dwarf::arm32::CNTFRQ, 32, "int32"},
      register_mapping{"cntpct", dwarf::arm32::CNTPCT, dwarf::arm32::CNTPCT, 64, "int64"},
      register_mapping{"cntvct", dwarf::arm32::CNTVCT, dwarf::arm32::CNTVCT, 64, "int64"},
      register_mapping{"cntp_tval", dwarf::arm32::CNTP_TVAL, dwarf::arm32::CNTP_TVAL, 32, "int32"},
      register_mapping{"cntp_ctl", dwarf::arm32::CNTP_CTL, dwarf::arm32::CNTP_CTL, 32, "int32"},
      register_mapping{"cntp_cval", dwarf::arm32::CNTP_CVAL, dwarf::arm32::CNTP_CVAL, 64, "int64"},
      register_mapping{"cntv_tval", dwarf::arm32::CNTV_TVAL, dwarf::arm32::CNTV_TVAL, 32, "int32"},
      register_mapping{"cntv_ctl", dwarf::arm32::CNTV_CTL, dwarf::arm32::CNTV_CTL, 32, "int32"},
      register_mapping{"cntv_cval", dwarf::arm32::CNTV_CVAL, dwarf::arm32::CNTV_CVAL, 64, "int64"},
      register_mapping{"cnthp_tval", dwarf::arm32::CNTHP_TVAL, dwarf::arm32::CNTHP_TVAL, 32, "int32"},
      register_mapping{"cnthp_ctl", dwarf::arm32::CNTHP_CTL, dwarf::arm32::CNTHP_CTL, 32, "int32"},
      register_mapping{"cnthp_cval", dwarf::arm32::CNTHP_CVAL, dwarf::arm32::CNTHP_CVAL, 64, "int64"},
      register_mapping{"cntvoff", dwarf::arm32::CNTVOFF, dwarf::arm32::CNTVOFF, 64, "int64"},
      register_mapping{"cnthctl", dwarf::arm32::CNTHCTL, dwarf::arm32::CNTHCTL, 32, "int32"},
      register_mapping{"cnthv_tval", dwarf::arm32::CNTHV_TVAL, dwarf::arm32::CNTHV_TVAL, 32, "int32"},
      register_mapping{"cnthv_ctl", dwarf::arm32::CNTHV_CTL, dwarf::arm32::CNTHV_CTL, 32, "int32"},
      register_mapping{"cnthv_cval", dwarf::arm32::CNTHV_CVAL, dwarf::arm32::CNTHV_CVAL, 64, "int64"},
      register_mapping{"cnthps_tval", dwarf::arm32::CNTHPS_TVAL, dwarf::arm32::CNTHPS_TVAL, 32, "int32"},
      register_mapping{"cnthps_ctl", dwarf::arm32::CNTHPS_CTL, dwarf::arm32::CNTHPS_CTL, 32, "int32"},
      register_mapping{"cnthps_cval", dwarf::arm32::CNTHPS_CVAL, dwarf::arm32::CNTHPS_CVAL, 64, "int64"},
      register_mapping{"cnthvs_tval", dwarf::arm32::CNTHVS_TVAL, dwarf::arm32::CNTHVS_TVAL, 32, "int32"},
      register_mapping{"cnthvs_ctl", dwarf::arm32::CNTHVS_CTL, dwarf::arm32::CNTHVS_CTL, 32, "int32"},
      register_mapping{"cnthvs_cval", dwarf::arm32::CNTHVS_CVAL, dwarf::arm32::CNTHVS_CVAL, 64, "int64"},
      register_mapping{"cntkctl", dwarf::arm32::CNTKCTL, dwarf::arm32::CNTKCTL, 32, "int32"}};
};

// ============================================================================
// RISC-V 64
// ============================================================================
template <> struct register_traits<tags::riscv64> : register_traits_base<register_traits<tags::riscv64>> {
  static constexpr microfmt::array arch_layout{// GDB exactly matches DWARF for x0-x31
                                               register_mapping{"zero", 0, dwarf::riscv::X0, 64, "int64"},
                                               register_mapping{"ra", 1, dwarf::riscv::RA, 64, "code_ptr"},
                                               register_mapping{"sp", 2, dwarf::riscv::SP, 64, "data_ptr"},
                                               register_mapping{"gp", 3, dwarf::riscv::GP, 64, "data_ptr"},
                                               register_mapping{"tp", 4, dwarf::riscv::TP, 64, "data_ptr"},
                                               register_mapping{"t0", 5, dwarf::riscv::T0, 64, "int64"},
                                               register_mapping{"t1", 6, dwarf::riscv::T1, 64, "int64"},
                                               register_mapping{"t2", 7, dwarf::riscv::T2, 64, "int64"},
                                               register_mapping{"s0", 8, dwarf::riscv::S0, 64, "data_ptr"},
                                               register_mapping{"s1", 9, dwarf::riscv::S1, 64, "int64"},
                                               register_mapping{"a0", 10, dwarf::riscv::A0, 64, "int64"},
                                               register_mapping{"a1", 11, dwarf::riscv::A1, 64, "int64"},
                                               register_mapping{"a2", 12, dwarf::riscv::A2, 64, "int64"},
                                               register_mapping{"a3", 13, dwarf::riscv::A3, 64, "int64"},
                                               register_mapping{"a4", 14, dwarf::riscv::A4, 64, "int64"},
                                               register_mapping{"a5", 15, dwarf::riscv::A5, 64, "int64"},
                                               register_mapping{"a6", 16, dwarf::riscv::A6, 64, "int64"},
                                               register_mapping{"a7", 17, dwarf::riscv::A7, 64, "int64"},
                                               register_mapping{"s2", 18, dwarf::riscv::S2, 64, "int64"},
                                               register_mapping{"s3", 19, dwarf::riscv::S3, 64, "int64"},
                                               register_mapping{"s4", 20, dwarf::riscv::S4, 64, "int64"},
                                               register_mapping{"s5", 21, dwarf::riscv::S5, 64, "int64"},
                                               register_mapping{"s6", 22, dwarf::riscv::S6, 64, "int64"},
                                               register_mapping{"s7", 23, dwarf::riscv::S7, 64, "int64"},
                                               register_mapping{"s8", 24, dwarf::riscv::S8, 64, "int64"},
                                               register_mapping{"s9", 25, dwarf::riscv::S9, 64, "int64"},
                                               register_mapping{"s10", 26, dwarf::riscv::S10, 64, "int64"},
                                               register_mapping{"s11", 27, dwarf::riscv::S11, 64, "int64"},
                                               register_mapping{"t3", 28, dwarf::riscv::T3, 64, "int64"},
                                               register_mapping{"t4", 29, dwarf::riscv::T4, 64, "int64"},
                                               register_mapping{"t5", 30, dwarf::riscv::T5, 64, "int64"},
                                               register_mapping{"t6", 31, dwarf::riscv::T6, 64, "int64"},

                                               // Divergence: GDB PC is 32, DWARF PC is 65
                                               register_mapping{"pc", 32, dwarf::riscv::PC, 64, "code_ptr"}};

  static constexpr microfmt::array extended_arch_layout{
      register_mapping{"f0", 33, dwarf::riscv::F0, 64, "ieee_double"},
      register_mapping{"f1", 34, dwarf::riscv::F1, 64, "ieee_double"},
      register_mapping{"f2", 35, dwarf::riscv::F2, 64, "ieee_double"},
      register_mapping{"f3", 36, dwarf::riscv::F3, 64, "ieee_double"},
      register_mapping{"f4", 37, dwarf::riscv::F4, 64, "ieee_double"},
      register_mapping{"f5", 38, dwarf::riscv::F5, 64, "ieee_double"},
      register_mapping{"f6", 39, dwarf::riscv::F6, 64, "ieee_double"},
      register_mapping{"f7", 40, dwarf::riscv::F7, 64, "ieee_double"},
      register_mapping{"f8", 41, dwarf::riscv::F8, 64, "ieee_double"},
      register_mapping{"f9", 42, dwarf::riscv::F9, 64, "ieee_double"},
      register_mapping{"f10", 43, dwarf::riscv::F10, 64, "ieee_double"},
      register_mapping{"f11", 44, dwarf::riscv::F11, 64, "ieee_double"},
      register_mapping{"f12", 45, dwarf::riscv::F12, 64, "ieee_double"},
      register_mapping{"f13", 46, dwarf::riscv::F13, 64, "ieee_double"},
      register_mapping{"f14", 47, dwarf::riscv::F14, 64, "ieee_double"},
      register_mapping{"f15", 48, dwarf::riscv::F15, 64, "ieee_double"},
      register_mapping{"f16", 49, dwarf::riscv::F16, 64, "ieee_double"},
      register_mapping{"f17", 50, dwarf::riscv::F17, 64, "ieee_double"},
      register_mapping{"f18", 51, dwarf::riscv::F18, 64, "ieee_double"},
      register_mapping{"f19", 52, dwarf::riscv::F19, 64, "ieee_double"},
      register_mapping{"f20", 53, dwarf::riscv::F20, 64, "ieee_double"},
      register_mapping{"f21", 54, dwarf::riscv::F21, 64, "ieee_double"},
      register_mapping{"f22", 55, dwarf::riscv::F22, 64, "ieee_double"},
      register_mapping{"f23", 56, dwarf::riscv::F23, 64, "ieee_double"},
      register_mapping{"f24", 57, dwarf::riscv::F24, 64, "ieee_double"},
      register_mapping{"f25", 58, dwarf::riscv::F25, 64, "ieee_double"},
      register_mapping{"f26", 59, dwarf::riscv::F26, 64, "ieee_double"},
      register_mapping{"f27", 60, dwarf::riscv::F27, 64, "ieee_double"},
      register_mapping{"f28", 61, dwarf::riscv::F28, 64, "ieee_double"},
      register_mapping{"f29", 62, dwarf::riscv::F29, 64, "ieee_double"},
      register_mapping{"f30", 63, dwarf::riscv::F30, 64, "ieee_double"},
      register_mapping{"f31", 64, dwarf::riscv::F31, 64, "ieee_double"}};

  static constexpr microfmt::array non_standard_arch_layout{
      register_mapping{"sstatus", dwarf::riscv::SSTATUS, dwarf::riscv::SSTATUS, 64, "int64"},
      register_mapping{"sepc", dwarf::riscv::SEPC, dwarf::riscv::SEPC, 64, "code_ptr"},
      register_mapping{"stval", dwarf::riscv::STVAL, dwarf::riscv::STVAL, 64, "data_ptr"},
      register_mapping{"satp", dwarf::riscv::SATP, dwarf::riscv::SATP, 64, "data_ptr"},
      register_mapping{"mstatus", dwarf::riscv::MSTATUS, dwarf::riscv::MSTATUS, 64, "int64"},
      register_mapping{"mepc", dwarf::riscv::MEPC, dwarf::riscv::MEPC, 64, "code_ptr"},
      register_mapping{"mtvec", dwarf::riscv::MTVEC, dwarf::riscv::MTVEC, 64, "code_ptr"}};
};

// ============================================================================
// RISC-V 32
// ============================================================================
template <> struct register_traits<tags::riscv32> : register_traits_base<register_traits<tags::riscv32>> {
  static constexpr microfmt::array arch_layout{register_mapping{"zero", 0, dwarf::riscv::X0, 32, "int32"},
                                               register_mapping{"ra", 1, dwarf::riscv::RA, 32, "code_ptr"},
                                               register_mapping{"sp", 2, dwarf::riscv::SP, 32, "data_ptr"},
                                               register_mapping{"gp", 3, dwarf::riscv::GP, 32, "data_ptr"},
                                               register_mapping{"tp", 4, dwarf::riscv::TP, 32, "data_ptr"},
                                               register_mapping{"t0", 5, dwarf::riscv::T0, 32, "int32"},
                                               register_mapping{"t1", 6, dwarf::riscv::T1, 32, "int32"},
                                               register_mapping{"t2", 7, dwarf::riscv::T2, 32, "int32"},
                                               register_mapping{"s0", 8, dwarf::riscv::S0, 32, "data_ptr"},
                                               register_mapping{"s1", 9, dwarf::riscv::S1, 32, "int32"},
                                               register_mapping{"a0", 10, dwarf::riscv::A0, 32, "int32"},
                                               register_mapping{"a1", 11, dwarf::riscv::A1, 32, "int32"},
                                               register_mapping{"a2", 12, dwarf::riscv::A2, 32, "int32"},
                                               register_mapping{"a3", 13, dwarf::riscv::A3, 32, "int32"},
                                               register_mapping{"a4", 14, dwarf::riscv::A4, 32, "int32"},
                                               register_mapping{"a5", 15, dwarf::riscv::A5, 32, "int32"},
                                               register_mapping{"a6", 16, dwarf::riscv::A6, 32, "int32"},
                                               register_mapping{"a7", 17, dwarf::riscv::A7, 32, "int32"},
                                               register_mapping{"s2", 18, dwarf::riscv::S2, 32, "int32"},
                                               register_mapping{"s3", 19, dwarf::riscv::S3, 32, "int32"},
                                               register_mapping{"s4", 20, dwarf::riscv::S4, 32, "int32"},
                                               register_mapping{"s5", 21, dwarf::riscv::S5, 32, "int32"},
                                               register_mapping{"s6", 22, dwarf::riscv::S6, 32, "int32"},
                                               register_mapping{"s7", 23, dwarf::riscv::S7, 32, "int32"},
                                               register_mapping{"s8", 24, dwarf::riscv::S8, 32, "int32"},
                                               register_mapping{"s9", 25, dwarf::riscv::S9, 32, "int32"},
                                               register_mapping{"s10", 26, dwarf::riscv::S10, 32, "int32"},
                                               register_mapping{"s11", 27, dwarf::riscv::S11, 32, "int32"},
                                               register_mapping{"t3", 28, dwarf::riscv::T3, 32, "int32"},
                                               register_mapping{"t4", 29, dwarf::riscv::T4, 32, "int32"},
                                               register_mapping{"t5", 30, dwarf::riscv::T5, 32, "int32"},
                                               register_mapping{"t6", 31, dwarf::riscv::T6, 32, "int32"},
                                               register_mapping{"pc", 32, dwarf::riscv::PC, 32, "code_ptr"}};

  static constexpr microfmt::array extended_arch_layout{
      register_mapping{"f0", 33, dwarf::riscv::F0, 32, "ieee_single"},
      register_mapping{"f1", 34, dwarf::riscv::F1, 32, "ieee_single"},
      register_mapping{"f2", 35, dwarf::riscv::F2, 32, "ieee_single"},
      register_mapping{"f3", 36, dwarf::riscv::F3, 32, "ieee_single"},
      register_mapping{"f4", 37, dwarf::riscv::F4, 32, "ieee_single"},
      register_mapping{"f5", 38, dwarf::riscv::F5, 32, "ieee_single"},
      register_mapping{"f6", 39, dwarf::riscv::F6, 32, "ieee_single"},
      register_mapping{"f7", 40, dwarf::riscv::F7, 32, "ieee_single"},
      register_mapping{"f8", 41, dwarf::riscv::F8, 32, "ieee_single"},
      register_mapping{"f9", 42, dwarf::riscv::F9, 32, "ieee_single"},
      register_mapping{"f10", 43, dwarf::riscv::F10, 32, "ieee_single"},
      register_mapping{"f11", 44, dwarf::riscv::F11, 32, "ieee_single"},
      register_mapping{"f12", 45, dwarf::riscv::F12, 32, "ieee_single"},
      register_mapping{"f13", 46, dwarf::riscv::F13, 32, "ieee_single"},
      register_mapping{"f14", 47, dwarf::riscv::F14, 32, "ieee_single"},
      register_mapping{"f15", 48, dwarf::riscv::F15, 32, "ieee_single"},
      register_mapping{"f16", 49, dwarf::riscv::F16, 32, "ieee_single"},
      register_mapping{"f17", 50, dwarf::riscv::F17, 32, "ieee_single"},
      register_mapping{"f18", 51, dwarf::riscv::F18, 32, "ieee_single"},
      register_mapping{"f19", 52, dwarf::riscv::F19, 32, "ieee_single"},
      register_mapping{"f20", 53, dwarf::riscv::F20, 32, "ieee_single"},
      register_mapping{"f21", 54, dwarf::riscv::F21, 32, "ieee_single"},
      register_mapping{"f22", 55, dwarf::riscv::F22, 32, "ieee_single"},
      register_mapping{"f23", 56, dwarf::riscv::F23, 32, "ieee_single"},
      register_mapping{"f24", 57, dwarf::riscv::F24, 32, "ieee_single"},
      register_mapping{"f25", 58, dwarf::riscv::F25, 32, "ieee_single"},
      register_mapping{"f26", 59, dwarf::riscv::F26, 32, "ieee_single"},
      register_mapping{"f27", 60, dwarf::riscv::F27, 32, "ieee_single"},
      register_mapping{"f28", 61, dwarf::riscv::F28, 32, "ieee_single"},
      register_mapping{"f29", 62, dwarf::riscv::F29, 32, "ieee_single"},
      register_mapping{"f30", 63, dwarf::riscv::F30, 32, "ieee_single"},
      register_mapping{"f31", 64, dwarf::riscv::F31, 32, "ieee_single"}};

  static constexpr microfmt::array non_standard_arch_layout{
      register_mapping{"sstatus", dwarf::riscv::SSTATUS, dwarf::riscv::SSTATUS, 32, "int32"},
      register_mapping{"sepc", dwarf::riscv::SEPC, dwarf::riscv::SEPC, 32, "code_ptr"},
      register_mapping{"stval", dwarf::riscv::STVAL, dwarf::riscv::STVAL, 32, "data_ptr"},
      register_mapping{"satp", dwarf::riscv::SATP, dwarf::riscv::SATP, 32, "data_ptr"},
      register_mapping{"mstatus", dwarf::riscv::MSTATUS, dwarf::riscv::MSTATUS, 32, "int32"},
      register_mapping{"mepc", dwarf::riscv::MEPC, dwarf::riscv::MEPC, 32, "code_ptr"},
      register_mapping{"mtvec", dwarf::riscv::MTVEC, dwarf::riscv::MTVEC, 32, "code_ptr"}};
};

} // namespace microfmt::gdb