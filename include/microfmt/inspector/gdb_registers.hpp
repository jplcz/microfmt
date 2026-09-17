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
                                               register_mapping{"rax", 0, dwarf::x86_64::rax, 64, "int64"},
                                               register_mapping{"rbx", 1, dwarf::x86_64::rbx, 64, "int64"},
                                               register_mapping{"rcx", 2, dwarf::x86_64::rcx, 64, "int64"},
                                               register_mapping{"rdx", 3, dwarf::x86_64::rdx, 64, "int64"},
                                               register_mapping{"rsi", 4, dwarf::x86_64::rsi, 64, "int64"},
                                               register_mapping{"rdi", 5, dwarf::x86_64::rdi, 64, "int64"},
                                               register_mapping{"rbp", 6, dwarf::x86_64::rbp, 64, "data_ptr"},
                                               register_mapping{"rsp", 7, dwarf::x86_64::rsp, 64, "data_ptr"},
                                               register_mapping{"r8", 8, dwarf::x86_64::r8, 64, "int64"},
                                               register_mapping{"r9", 9, dwarf::x86_64::r9, 64, "int64"},
                                               register_mapping{"r10", 10, dwarf::x86_64::r10, 64, "int64"},
                                               register_mapping{"r11", 11, dwarf::x86_64::r11, 64, "int64"},
                                               register_mapping{"r12", 12, dwarf::x86_64::r12, 64, "int64"},
                                               register_mapping{"r13", 13, dwarf::x86_64::r13, 64, "int64"},
                                               register_mapping{"r14", 14, dwarf::x86_64::r14, 64, "int64"},
                                               register_mapping{"r15", 15, dwarf::x86_64::r15, 64, "int64"},
                                               register_mapping{"rip", 16, dwarf::x86_64::rip, 64, "code_ptr"},
                                               register_mapping{"eflags", 17, dwarf::x86_64::eflags, 32, "int32"},
                                               register_mapping{"cs", 18, dwarf::x86_64::cs, 32, "int32"},
                                               register_mapping{"ss", 19, dwarf::x86_64::ss, 32, "int32"},
                                               register_mapping{"ds", 20, dwarf::x86_64::ds, 32, "int32"},
                                               register_mapping{"es", 21, dwarf::x86_64::es, 32, "int32"},
                                               register_mapping{"fs", 22, dwarf::x86_64::fs, 32, "int32"},
                                               register_mapping{"gs", 23, dwarf::x86_64::gs, 32, "int32"}};

  static constexpr microfmt::array extended_arch_layout{register_mapping{"xmm0", 40, dwarf::x86_64::xmm0, 128, ""},
                                                        register_mapping{"xmm1", 41, dwarf::x86_64::xmm1, 128, ""},
                                                        register_mapping{"xmm2", 42, dwarf::x86_64::xmm2, 128, ""},
                                                        register_mapping{"xmm3", 43, dwarf::x86_64::xmm3, 128, ""},
                                                        register_mapping{"xmm4", 44, dwarf::x86_64::xmm4, 128, ""},
                                                        register_mapping{"xmm5", 45, dwarf::x86_64::xmm5, 128, ""},
                                                        register_mapping{"xmm6", 46, dwarf::x86_64::xmm6, 128, ""},
                                                        register_mapping{"xmm7", 47, dwarf::x86_64::xmm7, 128, ""},
                                                        register_mapping{"xmm8", 48, dwarf::x86_64::xmm8, 128, ""},
                                                        register_mapping{"xmm9", 49, dwarf::x86_64::xmm9, 128, ""},
                                                        register_mapping{"xmm10", 50, dwarf::x86_64::xmm10, 128, ""},
                                                        register_mapping{"xmm11", 51, dwarf::x86_64::xmm11, 128, ""},
                                                        register_mapping{"xmm12", 52, dwarf::x86_64::xmm12, 128, ""},
                                                        register_mapping{"xmm13", 53, dwarf::x86_64::xmm13, 128, ""},
                                                        register_mapping{"xmm14", 54, dwarf::x86_64::xmm14, 128, ""},
                                                        register_mapping{"xmm15", 55, dwarf::x86_64::xmm15, 128, ""}};

  static constexpr microfmt::array non_standard_arch_layout{
      register_mapping{"fs_base", 58, dwarf::x86_64::fs_base, 64, "data_ptr"},
      register_mapping{"gs_base", 59, dwarf::x86_64::gs_base, 64, "data_ptr"},
      register_mapping{"kernel_gs_base", 66, dwarf::x86_64::kernel_gs_base, 64, "data_ptr"},
      register_mapping{"cr0", 100, dwarf::x86_64::cr0, 64, "int64"},
      register_mapping{"cr2", 102, dwarf::x86_64::cr2, 64, "data_ptr"},
      register_mapping{"cr3", 103, dwarf::x86_64::cr3, 64, "data_ptr"},
      register_mapping{"cr4", 104, dwarf::x86_64::cr4, 64, "int64"},
      register_mapping{"cr8", 108, dwarf::x86_64::cr8, 64, "int64"},
      register_mapping{"dr0", 120, dwarf::x86_64::dr0, 64, "data_ptr"},
      register_mapping{"dr1", 121, dwarf::x86_64::dr1, 64, "data_ptr"},
      register_mapping{"dr2", 122, dwarf::x86_64::dr2, 64, "data_ptr"},
      register_mapping{"dr3", 123, dwarf::x86_64::dr3, 64, "data_ptr"},
      register_mapping{"dr6", 126, dwarf::x86_64::dr6, 64, "int64"},
      register_mapping{"dr7", 127, dwarf::x86_64::dr7, 64, "int64"}};
};

// ============================================================================
// x86 (IA-32)
// ============================================================================
template <> struct register_traits<tags::x86> : register_traits_base<register_traits<tags::x86>> {
  static constexpr microfmt::array arch_layout{// x86 GDB and DWARF match exactly for the first 9 registers
                                               register_mapping{"eax", 0, dwarf::x86::eax, 32, "int32"},
                                               register_mapping{"ecx", 1, dwarf::x86::ecx, 32, "int32"},
                                               register_mapping{"edx", 2, dwarf::x86::edx, 32, "int32"},
                                               register_mapping{"ebx", 3, dwarf::x86::ebx, 32, "int32"},
                                               register_mapping{"esp", 4, dwarf::x86::esp, 32, "data_ptr"},
                                               register_mapping{"ebp", 5, dwarf::x86::ebp, 32, "data_ptr"},
                                               register_mapping{"esi", 6, dwarf::x86::esi, 32, "int32"},
                                               register_mapping{"edi", 7, dwarf::x86::edi, 32, "int32"},
                                               register_mapping{"eip", 8, dwarf::x86::eip, 32, "code_ptr"}};

  static constexpr microfmt::array extended_arch_layout{register_mapping{"st0", 16, dwarf::x86::st0, 80, "i387_ext"},
                                                        register_mapping{"st1", 17, dwarf::x86::st1, 80, "i387_ext"},
                                                        register_mapping{"st2", 18, dwarf::x86::st2, 80, "i387_ext"},
                                                        register_mapping{"st3", 19, dwarf::x86::st3, 80, "i387_ext"},
                                                        register_mapping{"st4", 20, dwarf::x86::st4, 80, "i387_ext"},
                                                        register_mapping{"st5", 21, dwarf::x86::st5, 80, "i387_ext"},
                                                        register_mapping{"st6", 22, dwarf::x86::st6, 80, "i387_ext"},
                                                        register_mapping{"st7", 23, dwarf::x86::st7, 80, "i387_ext"},
                                                        register_mapping{"xmm0", 32, dwarf::x86::xmm0, 128, ""},
                                                        register_mapping{"xmm1", 33, dwarf::x86::xmm1, 128, ""},
                                                        register_mapping{"xmm2", 34, dwarf::x86::xmm2, 128, ""},
                                                        register_mapping{"xmm3", 35, dwarf::x86::xmm3, 128, ""},
                                                        register_mapping{"xmm4", 36, dwarf::x86::xmm4, 128, ""},
                                                        register_mapping{"xmm5", 37, dwarf::x86::xmm5, 128, ""},
                                                        register_mapping{"xmm6", 38, dwarf::x86::xmm6, 128, ""},
                                                        register_mapping{"xmm7", 39, dwarf::x86::xmm7, 128, ""}};

  static constexpr microfmt::array non_standard_arch_layout{
      register_mapping{"gs_base", 58, dwarf::x86::gs_base, 32, "data_ptr"},
      register_mapping{"fs_base", 59, dwarf::x86::fs_base, 32, "data_ptr"},
      register_mapping{"cr0", 100, dwarf::x86::cr0, 32, "int32"},
      register_mapping{"cr3", 103, dwarf::x86::cr3, 32, "data_ptr"},
      register_mapping{"cr4", 104, dwarf::x86::cr4, 32, "int32"}};
};

// ============================================================================
// AArch64 (ARM 64-bit)
// ============================================================================
template <> struct register_traits<tags::aarch64> : register_traits_base<register_traits<tags::aarch64>> {
  static constexpr microfmt::array arch_layout{
      register_mapping{"x0", 0, dwarf::aarch64::x0, 64, "int64"},
      register_mapping{"x1", 1, dwarf::aarch64::x1, 64, "int64"},
      register_mapping{"x2", 2, dwarf::aarch64::x2, 64, "int64"},
      register_mapping{"x3", 3, dwarf::aarch64::x3, 64, "int64"},
      register_mapping{"x4", 4, dwarf::aarch64::x4, 64, "int64"},
      register_mapping{"x5", 5, dwarf::aarch64::x5, 64, "int64"},
      register_mapping{"x6", 6, dwarf::aarch64::x6, 64, "int64"},
      register_mapping{"x7", 7, dwarf::aarch64::x7, 64, "int64"},
      register_mapping{"x8", 8, dwarf::aarch64::x8, 64, "int64"},
      register_mapping{"x9", 9, dwarf::aarch64::x9, 64, "int64"},
      register_mapping{"x10", 10, dwarf::aarch64::x10, 64, "int64"},
      register_mapping{"x11", 11, dwarf::aarch64::x11, 64, "int64"},
      register_mapping{"x12", 12, dwarf::aarch64::x12, 64, "int64"},
      register_mapping{"x13", 13, dwarf::aarch64::x13, 64, "int64"},
      register_mapping{"x14", 14, dwarf::aarch64::x14, 64, "int64"},
      register_mapping{"x15", 15, dwarf::aarch64::x15, 64, "int64"},
      register_mapping{"x16", 16, dwarf::aarch64::x16, 64, "int64"},
      register_mapping{"x17", 17, dwarf::aarch64::x17, 64, "int64"},
      register_mapping{"x18", 18, dwarf::aarch64::x18, 64, "int64"},
      register_mapping{"x19", 19, dwarf::aarch64::x19, 64, "int64"},
      register_mapping{"x20", 20, dwarf::aarch64::x20, 64, "int64"},
      register_mapping{"x21", 21, dwarf::aarch64::x21, 64, "int64"},
      register_mapping{"x22", 22, dwarf::aarch64::x22, 64, "int64"},
      register_mapping{"x23", 23, dwarf::aarch64::x23, 64, "int64"},
      register_mapping{"x24", 24, dwarf::aarch64::x24, 64, "int64"},
      register_mapping{"x25", 25, dwarf::aarch64::x25, 64, "int64"},
      register_mapping{"x26", 26, dwarf::aarch64::x26, 64, "int64"},
      register_mapping{"x27", 27, dwarf::aarch64::x27, 64, "int64"},
      register_mapping{"x28", 28, dwarf::aarch64::x28, 64, "int64"},
      register_mapping{"x29", 29, dwarf::aarch64::fp, 64, "data_ptr"},
      register_mapping{"x30", 30, dwarf::aarch64::lr, 64, "code_ptr"},
      register_mapping{"sp", 31, dwarf::aarch64::sp, 64, "data_ptr"},
      register_mapping{"pc", 32, dwarf::aarch64::pc, 64, "code_ptr"},
      register_mapping{"cpsr", 33, dwarf::aarch64::pstate, 32, "int32"} // PSTATE sent as 32-bit CPSR equivalent
  };

  static constexpr microfmt::array extended_arch_layout{register_mapping{"v0", 34, dwarf::aarch64::v0, 128, ""},
                                                        register_mapping{"v1", 35, dwarf::aarch64::v1, 128, ""},
                                                        register_mapping{"v2", 36, dwarf::aarch64::v2, 128, ""},
                                                        register_mapping{"v3", 37, dwarf::aarch64::v3, 128, ""},
                                                        register_mapping{"v4", 38, dwarf::aarch64::v4, 128, ""},
                                                        register_mapping{"v5", 39, dwarf::aarch64::v5, 128, ""},
                                                        register_mapping{"v6", 40, dwarf::aarch64::v6, 128, ""},
                                                        register_mapping{"v7", 41, dwarf::aarch64::v7, 128, ""},
                                                        register_mapping{"v8", 42, dwarf::aarch64::v8, 128, ""},
                                                        register_mapping{"v9", 43, dwarf::aarch64::v9, 128, ""},
                                                        register_mapping{"v10", 44, dwarf::aarch64::v10, 128, ""},
                                                        register_mapping{"v11", 45, dwarf::aarch64::v11, 128, ""},
                                                        register_mapping{"v12", 46, dwarf::aarch64::v12, 128, ""},
                                                        register_mapping{"v13", 47, dwarf::aarch64::v13, 128, ""},
                                                        register_mapping{"v14", 48, dwarf::aarch64::v14, 128, ""},
                                                        register_mapping{"v15", 49, dwarf::aarch64::v15, 128, ""},
                                                        register_mapping{"v16", 50, dwarf::aarch64::v16, 128, ""},
                                                        register_mapping{"v17", 51, dwarf::aarch64::v17, 128, ""},
                                                        register_mapping{"v18", 52, dwarf::aarch64::v18, 128, ""},
                                                        register_mapping{"v19", 53, dwarf::aarch64::v19, 128, ""},
                                                        register_mapping{"v20", 54, dwarf::aarch64::v20, 128, ""},
                                                        register_mapping{"v21", 55, dwarf::aarch64::v21, 128, ""},
                                                        register_mapping{"v22", 56, dwarf::aarch64::v22, 128, ""},
                                                        register_mapping{"v23", 57, dwarf::aarch64::v23, 128, ""},
                                                        register_mapping{"v24", 58, dwarf::aarch64::v24, 128, ""},
                                                        register_mapping{"v25", 59, dwarf::aarch64::v25, 128, ""},
                                                        register_mapping{"v26", 60, dwarf::aarch64::v26, 128, ""},
                                                        register_mapping{"v27", 61, dwarf::aarch64::v27, 128, ""},
                                                        register_mapping{"v28", 62, dwarf::aarch64::v28, 128, ""},
                                                        register_mapping{"v29", 63, dwarf::aarch64::v29, 128, ""},
                                                        register_mapping{"v30", 64, dwarf::aarch64::v30, 128, ""},
                                                        register_mapping{"v31", 65, dwarf::aarch64::v31, 128, ""}};

  static constexpr microfmt::array non_standard_arch_layout{
      register_mapping{"tpidr_el0", dwarf::aarch64::tpidr_el0, dwarf::aarch64::tpidr_el0, 64, "data_ptr"},
      register_mapping{"tpidrro_el0", dwarf::aarch64::tpidrro_el0, dwarf::aarch64::tpidrro_el0, 64, "data_ptr"},
      register_mapping{"tpidr_el1", dwarf::aarch64::tpidr_el1, dwarf::aarch64::tpidr_el1, 64, "data_ptr"},
      register_mapping{"tpidr_el2", dwarf::aarch64::tpidr_el2, dwarf::aarch64::tpidr_el2, 64, "data_ptr"},
      register_mapping{"tpidr_el3", dwarf::aarch64::tpidr_el3, dwarf::aarch64::tpidr_el3, 64, "data_ptr"},
      register_mapping{"sp_el0", dwarf::aarch64::sp_el0, dwarf::aarch64::sp_el0, 64, "data_ptr"},
      register_mapping{"sp_el1", dwarf::aarch64::sp_el1, dwarf::aarch64::sp_el1, 64, "data_ptr"},
      register_mapping{"elr_el1", dwarf::aarch64::elr_el1, dwarf::aarch64::elr_el1, 64, "code_ptr"},
      register_mapping{"spsr_el1", dwarf::aarch64::spsr_el1, dwarf::aarch64::spsr_el1, 64, "int64"},
      register_mapping{"sctlr_el1", dwarf::aarch64::sctlr_el1, dwarf::aarch64::sctlr_el1, 64, "int64"},
      register_mapping{"vbar_el1", dwarf::aarch64::vbar_el1, dwarf::aarch64::vbar_el1, 64, "code_ptr"},
      register_mapping{"vbar_el2", dwarf::aarch64::vbar_el2, dwarf::aarch64::vbar_el2, 64, "code_ptr"},
      register_mapping{"sp_el2", dwarf::aarch64::sp_el2, dwarf::aarch64::sp_el2, 64, "data_ptr"},
      register_mapping{"sp_el3", dwarf::aarch64::sp_el3, dwarf::aarch64::sp_el3, 64, "data_ptr"},
      register_mapping{"elr_el2", dwarf::aarch64::elr_el2, dwarf::aarch64::elr_el2, 64, "code_ptr"},
      register_mapping{"elr_el3", dwarf::aarch64::elr_el3, dwarf::aarch64::elr_el3, 64, "code_ptr"},
      register_mapping{"spsr_el2", dwarf::aarch64::spsr_el2, dwarf::aarch64::spsr_el2, 64, "int64"},
      register_mapping{"spsr_el3", dwarf::aarch64::spsr_el3, dwarf::aarch64::spsr_el3, 64, "int64"},
      register_mapping{"sctlr_el2", dwarf::aarch64::sctlr_el2, dwarf::aarch64::sctlr_el2, 64, "int64"},
      register_mapping{"sctlr_el3", dwarf::aarch64::sctlr_el3, dwarf::aarch64::sctlr_el3, 64, "int64"},
      register_mapping{"vbar_el3", dwarf::aarch64::vbar_el3, dwarf::aarch64::vbar_el3, 64, "code_ptr"},
      register_mapping{"ttbr0_el1", dwarf::aarch64::ttbr0_el1, dwarf::aarch64::ttbr0_el1, 64, "data_ptr"},
      register_mapping{"ttbr1_el1", dwarf::aarch64::ttbr1_el1, dwarf::aarch64::ttbr1_el1, 64, "data_ptr"},
      register_mapping{"tcr_el1", dwarf::aarch64::tcr_el1, dwarf::aarch64::tcr_el1, 64, "int64"},
      register_mapping{"mair_el1", dwarf::aarch64::mair_el1, dwarf::aarch64::mair_el1, 64, "int64"},
      register_mapping{"amair_el1", dwarf::aarch64::amair_el1, dwarf::aarch64::amair_el1, 64, "int64"},
      register_mapping{"esr_el1", dwarf::aarch64::esr_el1, dwarf::aarch64::esr_el1, 64, "int64"},
      register_mapping{"far_el1", dwarf::aarch64::far_el1, dwarf::aarch64::far_el1, 64, "data_ptr"},
      register_mapping{"par_el1", dwarf::aarch64::par_el1, dwarf::aarch64::par_el1, 64, "int64"},
      register_mapping{"contextidr_el1", dwarf::aarch64::contextidr_el1, dwarf::aarch64::contextidr_el1, 64, "int64"},
      register_mapping{"cpacr_el1", dwarf::aarch64::cpacr_el1, dwarf::aarch64::cpacr_el1, 64, "int64"},
      register_mapping{"midr_el1", dwarf::aarch64::midr_el1, dwarf::aarch64::midr_el1, 64, "int64"},
      register_mapping{"mpidr_el1", dwarf::aarch64::mpidr_el1, dwarf::aarch64::mpidr_el1, 64, "int64"},
      register_mapping{"revidr_el1", dwarf::aarch64::revidr_el1, dwarf::aarch64::revidr_el1, 64, "int64"},
      register_mapping{"id_aa64pfr0_el1", dwarf::aarch64::id_aa64pfr0_el1, dwarf::aarch64::id_aa64pfr0_el1, 64,
                       "int64"},
      register_mapping{"id_aa64mmfr0_el1", dwarf::aarch64::id_aa64mmfr0_el1, dwarf::aarch64::id_aa64mmfr0_el1, 64,
                       "int64"},
      register_mapping{"id_aa64isar0_el1", dwarf::aarch64::id_aa64isar0_el1, dwarf::aarch64::id_aa64isar0_el1, 64,
                       "int64"},
      register_mapping{"ttbr0_el2", dwarf::aarch64::ttbr0_el2, dwarf::aarch64::ttbr0_el2, 64, "data_ptr"},
      register_mapping{"tcr_el2", dwarf::aarch64::tcr_el2, dwarf::aarch64::tcr_el2, 64, "int64"},
      register_mapping{"mair_el2", dwarf::aarch64::mair_el2, dwarf::aarch64::mair_el2, 64, "int64"},
      register_mapping{"esr_el2", dwarf::aarch64::esr_el2, dwarf::aarch64::esr_el2, 64, "int64"},
      register_mapping{"far_el2", dwarf::aarch64::far_el2, dwarf::aarch64::far_el2, 64, "data_ptr"},
      register_mapping{"hcr_el2", dwarf::aarch64::hcr_el2, dwarf::aarch64::hcr_el2, 64, "int64"},
      register_mapping{"vtcr_el2", dwarf::aarch64::vtcr_el2, dwarf::aarch64::vtcr_el2, 64, "int64"},
      register_mapping{"vttbr_el2", dwarf::aarch64::vttbr_el2, dwarf::aarch64::vttbr_el2, 64, "data_ptr"},
      register_mapping{"scr_el3", dwarf::aarch64::scr_el3, dwarf::aarch64::scr_el3, 64, "int64"},
      register_mapping{"esr_el3", dwarf::aarch64::esr_el3, dwarf::aarch64::esr_el3, 64, "int64"},
      register_mapping{"far_el3", dwarf::aarch64::far_el3, dwarf::aarch64::far_el3, 64, "data_ptr"},
      register_mapping{"currentel", dwarf::aarch64::currentel, dwarf::aarch64::currentel, 64, "int64"},
      register_mapping{"daif", dwarf::aarch64::daif, dwarf::aarch64::daif, 64, "int64"},
      register_mapping{"nzcv", dwarf::aarch64::nzcv, dwarf::aarch64::nzcv, 64, "int64"},
      register_mapping{"cntfrq_el0", dwarf::aarch64::cntfrq_el0, dwarf::aarch64::cntfrq_el0, 32, "int32"},
      register_mapping{"cntpct_el0", dwarf::aarch64::cntpct_el0, dwarf::aarch64::cntpct_el0, 64, "int64"},
      register_mapping{"cntvct_el0", dwarf::aarch64::cntvct_el0, dwarf::aarch64::cntvct_el0, 64, "int64"},
      register_mapping{"cntp_tval_el0", dwarf::aarch64::cntp_tval_el0, dwarf::aarch64::cntp_tval_el0, 32, "int32"},
      register_mapping{"cntp_ctl_el0", dwarf::aarch64::cntp_ctl_el0, dwarf::aarch64::cntp_ctl_el0, 32, "int32"},
      register_mapping{"cntp_cval_el0", dwarf::aarch64::cntp_cval_el0, dwarf::aarch64::cntp_cval_el0, 64, "int64"},
      register_mapping{"cntv_tval_el0", dwarf::aarch64::cntv_tval_el0, dwarf::aarch64::cntv_tval_el0, 32, "int32"},
      register_mapping{"cntv_ctl_el0", dwarf::aarch64::cntv_ctl_el0, dwarf::aarch64::cntv_ctl_el0, 32, "int32"},
      register_mapping{"cntv_cval_el0", dwarf::aarch64::cntv_cval_el0, dwarf::aarch64::cntv_cval_el0, 64, "int64"},
      register_mapping{"cnthp_tval_el2", dwarf::aarch64::cnthp_tval_el2, dwarf::aarch64::cnthp_tval_el2, 32, "int32"},
      register_mapping{"cnthp_ctl_el2", dwarf::aarch64::cnthp_ctl_el2, dwarf::aarch64::cnthp_ctl_el2, 32, "int32"},
      register_mapping{"cnthp_cval_el2", dwarf::aarch64::cnthp_cval_el2, dwarf::aarch64::cnthp_cval_el2, 64, "int64"},
      register_mapping{"cntvoff_el2", dwarf::aarch64::cntvoff_el2, dwarf::aarch64::cntvoff_el2, 64, "int64"},
      register_mapping{"cnthctl_el2", dwarf::aarch64::cnthctl_el2, dwarf::aarch64::cnthctl_el2, 32, "int32"},
      register_mapping{"cnthv_tval_el2", dwarf::aarch64::cnthv_tval_el2, dwarf::aarch64::cnthv_tval_el2, 32, "int32"},
      register_mapping{"cnthv_ctl_el2", dwarf::aarch64::cnthv_ctl_el2, dwarf::aarch64::cnthv_ctl_el2, 32, "int32"},
      register_mapping{"cnthv_cval_el2", dwarf::aarch64::cnthv_cval_el2, dwarf::aarch64::cnthv_cval_el2, 64, "int64"},
      register_mapping{"cnthps_tval_el2", dwarf::aarch64::cnthps_tval_el2, dwarf::aarch64::cnthps_tval_el2, 32,
                       "int32"},
      register_mapping{"cnthps_ctl_el2", dwarf::aarch64::cnthps_ctl_el2, dwarf::aarch64::cnthps_ctl_el2, 32, "int32"},
      register_mapping{"cnthps_cval_el2", dwarf::aarch64::cnthps_cval_el2, dwarf::aarch64::cnthps_cval_el2, 64,
                       "int64"},
      register_mapping{"cnthvs_tval_el2", dwarf::aarch64::cnthvs_tval_el2, dwarf::aarch64::cnthvs_tval_el2, 32,
                       "int32"},
      register_mapping{"cnthvs_ctl_el2", dwarf::aarch64::cnthvs_ctl_el2, dwarf::aarch64::cnthvs_ctl_el2, 32, "int32"},
      register_mapping{"cnthvs_cval_el2", dwarf::aarch64::cnthvs_cval_el2, dwarf::aarch64::cnthvs_cval_el2, 64,
                       "int64"},
      register_mapping{"cntkctl_el1", dwarf::aarch64::cntkctl_el1, dwarf::aarch64::cntkctl_el1, 32, "int32"}};
};

// ============================================================================
// ARM32 (AArch32)
// ============================================================================
template <> struct register_traits<tags::arm32> : register_traits_base<register_traits<tags::arm32>> {
  static constexpr microfmt::array arch_layout{
      register_mapping{"r0", 0, dwarf::arm32::r0, 32, "int32"},
      register_mapping{"r1", 1, dwarf::arm32::r1, 32, "int32"},
      register_mapping{"r2", 2, dwarf::arm32::r2, 32, "int32"},
      register_mapping{"r3", 3, dwarf::arm32::r3, 32, "int32"},
      register_mapping{"r4", 4, dwarf::arm32::r4, 32, "int32"},
      register_mapping{"r5", 5, dwarf::arm32::r5, 32, "int32"},
      register_mapping{"r6", 6, dwarf::arm32::r6, 32, "int32"},
      register_mapping{"r7", 7, dwarf::arm32::r7, 32, "int32"},
      register_mapping{"r8", 8, dwarf::arm32::r8, 32, "int32"},
      register_mapping{"r9", 9, dwarf::arm32::r9, 32, "int32"},
      register_mapping{"r10", 10, dwarf::arm32::r10, 32, "int32"},
      register_mapping{"r11", 11, dwarf::arm32::fp, 32, "data_ptr"},
      register_mapping{"r12", 12, dwarf::arm32::r12, 32, "int32"},
      register_mapping{"sp", 13, dwarf::arm32::sp, 32, "data_ptr"},
      register_mapping{"lr", 14, dwarf::arm32::lr, 32, "code_ptr"},
      register_mapping{"pc", 15, dwarf::arm32::pc, 32, "code_ptr"},

      // Note: GDB ARM layouts typically map FPA floats at 16-24. CPSR is commonly 25.
      register_mapping{"cpsr", 25, dwarf::arm32::cpsr, 32, "int32"}};

  static constexpr microfmt::array extended_arch_layout{
      register_mapping{"d0", 26, dwarf::arm32::d0, 64, "ieee_double"},
      register_mapping{"d1", 27, dwarf::arm32::d1, 64, "ieee_double"},
      register_mapping{"d2", 28, dwarf::arm32::d2, 64, "ieee_double"},
      register_mapping{"d3", 29, dwarf::arm32::d3, 64, "ieee_double"},
      register_mapping{"d4", 30, dwarf::arm32::d4, 64, "ieee_double"},
      register_mapping{"d5", 31, dwarf::arm32::d5, 64, "ieee_double"},
      register_mapping{"d6", 32, dwarf::arm32::d6, 64, "ieee_double"},
      register_mapping{"d7", 33, dwarf::arm32::d7, 64, "ieee_double"},
      register_mapping{"d8", 34, dwarf::arm32::d8, 64, "ieee_double"},
      register_mapping{"d9", 35, dwarf::arm32::d9, 64, "ieee_double"},
      register_mapping{"d10", 36, dwarf::arm32::d10, 64, "ieee_double"},
      register_mapping{"d11", 37, dwarf::arm32::d11, 64, "ieee_double"},
      register_mapping{"d12", 38, dwarf::arm32::d12, 64, "ieee_double"},
      register_mapping{"d13", 39, dwarf::arm32::d13, 64, "ieee_double"},
      register_mapping{"d14", 40, dwarf::arm32::d14, 64, "ieee_double"},
      register_mapping{"d15", 41, dwarf::arm32::d15, 64, "ieee_double"},
      register_mapping{"d16", 42, dwarf::arm32::d16, 64, "ieee_double"},
      register_mapping{"d17", 43, dwarf::arm32::d17, 64, "ieee_double"},
      register_mapping{"d18", 44, dwarf::arm32::d18, 64, "ieee_double"},
      register_mapping{"d19", 45, dwarf::arm32::d19, 64, "ieee_double"},
      register_mapping{"d20", 46, dwarf::arm32::d20, 64, "ieee_double"},
      register_mapping{"d21", 47, dwarf::arm32::d21, 64, "ieee_double"},
      register_mapping{"d22", 48, dwarf::arm32::d22, 64, "ieee_double"},
      register_mapping{"d23", 49, dwarf::arm32::d23, 64, "ieee_double"},
      register_mapping{"d24", 50, dwarf::arm32::d24, 64, "ieee_double"},
      register_mapping{"d25", 51, dwarf::arm32::d25, 64, "ieee_double"},
      register_mapping{"d26", 52, dwarf::arm32::d26, 64, "ieee_double"},
      register_mapping{"d27", 53, dwarf::arm32::d27, 64, "ieee_double"},
      register_mapping{"d28", 54, dwarf::arm32::d28, 64, "ieee_double"},
      register_mapping{"d29", 55, dwarf::arm32::d29, 64, "ieee_double"},
      register_mapping{"d30", 56, dwarf::arm32::d30, 64, "ieee_double"},
      register_mapping{"d31", 57, dwarf::arm32::d31, 64, "ieee_double"}};

  static constexpr microfmt::array non_standard_arch_layout{
      register_mapping{"tpidrurw", dwarf::arm32::tpidrurw, dwarf::arm32::tpidrurw, 32, "data_ptr"},
      register_mapping{"tpidruro", dwarf::arm32::tpidruro, dwarf::arm32::tpidruro, 32, "data_ptr"},
      register_mapping{"tpidrprw", dwarf::arm32::tpidrprw, dwarf::arm32::tpidrprw, 32, "data_ptr"},
      register_mapping{"spsr", dwarf::arm32::spsr, dwarf::arm32::spsr, 32, "int32"},
      register_mapping{"apsr", dwarf::arm32::apsr, dwarf::arm32::apsr, 32, "int32"},
      register_mapping{"iapsr", dwarf::arm32::iapsr, dwarf::arm32::iapsr, 32, "int32"},
      register_mapping{"eapsr", dwarf::arm32::eapsr, dwarf::arm32::eapsr, 32, "int32"},
      register_mapping{"xpsr", dwarf::arm32::xpsr, dwarf::arm32::xpsr, 32, "int32"},
      register_mapping{"ipsr", dwarf::arm32::ipsr, dwarf::arm32::ipsr, 32, "int32"},
      register_mapping{"epsr", dwarf::arm32::epsr, dwarf::arm32::epsr, 32, "int32"},
      register_mapping{"iepsr", dwarf::arm32::iepsr, dwarf::arm32::iepsr, 32, "int32"},
      register_mapping{"msp", dwarf::arm32::msp, dwarf::arm32::msp, 32, "data_ptr"},
      register_mapping{"psp", dwarf::arm32::psp, dwarf::arm32::psp, 32, "data_ptr"},
      register_mapping{"primask", dwarf::arm32::primask, dwarf::arm32::primask, 32, "int32"},
      register_mapping{"basepri", dwarf::arm32::basepri, dwarf::arm32::basepri, 32, "int32"},
      register_mapping{"basepri_max", dwarf::arm32::basepri_max, dwarf::arm32::basepri_max, 32, "int32"},
      register_mapping{"faultmask", dwarf::arm32::faultmask, dwarf::arm32::faultmask, 32, "int32"},
      register_mapping{"control", dwarf::arm32::control, dwarf::arm32::control, 32, "int32"},
      register_mapping{"sctlr", dwarf::arm32::sctlr, dwarf::arm32::sctlr, 32, "int32"},
      register_mapping{"actlr", dwarf::arm32::actlr, dwarf::arm32::actlr, 32, "int32"},
      register_mapping{"cpacr", dwarf::arm32::cpacr, dwarf::arm32::cpacr, 32, "int32"},
      register_mapping{"ttbr0", dwarf::arm32::ttbr0, dwarf::arm32::ttbr0, 32, "data_ptr"},
      register_mapping{"ttbr1", dwarf::arm32::ttbr1, dwarf::arm32::ttbr1, 32, "data_ptr"},
      register_mapping{"ttbcr", dwarf::arm32::ttbcr, dwarf::arm32::ttbcr, 32, "int32"},
      register_mapping{"dacr", dwarf::arm32::dacr, dwarf::arm32::dacr, 32, "int32"},
      register_mapping{"dfsr", dwarf::arm32::dfsr, dwarf::arm32::dfsr, 32, "int32"},
      register_mapping{"ifsr", dwarf::arm32::ifsr, dwarf::arm32::ifsr, 32, "int32"},
      register_mapping{"dfar", dwarf::arm32::dfar, dwarf::arm32::dfar, 32, "data_ptr"},
      register_mapping{"ifar", dwarf::arm32::ifar, dwarf::arm32::ifar, 32, "data_ptr"},
      register_mapping{"vbar", dwarf::arm32::vbar, dwarf::arm32::vbar, 32, "code_ptr"},
      register_mapping{"contextidr", dwarf::arm32::contextidr, dwarf::arm32::contextidr, 32, "int32"},
      register_mapping{"mair0", dwarf::arm32::mair0, dwarf::arm32::mair0, 32, "int32"},
      register_mapping{"mair1", dwarf::arm32::mair1, dwarf::arm32::mair1, 32, "int32"},
      register_mapping{"amair0", dwarf::arm32::amair0, dwarf::arm32::amair0, 32, "int32"},
      register_mapping{"amair1", dwarf::arm32::amair1, dwarf::arm32::amair1, 32, "int32"},
      register_mapping{"midr", dwarf::arm32::midr, dwarf::arm32::midr, 32, "int32"},
      register_mapping{"mpidr", dwarf::arm32::mpidr, dwarf::arm32::mpidr, 32, "int32"},
      register_mapping{"cntfrq", dwarf::arm32::cntfrq, dwarf::arm32::cntfrq, 32, "int32"},
      register_mapping{"cntpct", dwarf::arm32::cntpct, dwarf::arm32::cntpct, 64, "int64"},
      register_mapping{"cntvct", dwarf::arm32::cntvct, dwarf::arm32::cntvct, 64, "int64"},
      register_mapping{"cntp_tval", dwarf::arm32::cntp_tval, dwarf::arm32::cntp_tval, 32, "int32"},
      register_mapping{"cntp_ctl", dwarf::arm32::cntp_ctl, dwarf::arm32::cntp_ctl, 32, "int32"},
      register_mapping{"cntp_cval", dwarf::arm32::cntp_cval, dwarf::arm32::cntp_cval, 64, "int64"},
      register_mapping{"cntv_tval", dwarf::arm32::cntv_tval, dwarf::arm32::cntv_tval, 32, "int32"},
      register_mapping{"cntv_ctl", dwarf::arm32::cntv_ctl, dwarf::arm32::cntv_ctl, 32, "int32"},
      register_mapping{"cntv_cval", dwarf::arm32::cntv_cval, dwarf::arm32::cntv_cval, 64, "int64"},
      register_mapping{"cnthp_tval", dwarf::arm32::cnthp_tval, dwarf::arm32::cnthp_tval, 32, "int32"},
      register_mapping{"cnthp_ctl", dwarf::arm32::cnthp_ctl, dwarf::arm32::cnthp_ctl, 32, "int32"},
      register_mapping{"cnthp_cval", dwarf::arm32::cnthp_cval, dwarf::arm32::cnthp_cval, 64, "int64"},
      register_mapping{"cntvoff", dwarf::arm32::cntvoff, dwarf::arm32::cntvoff, 64, "int64"},
      register_mapping{"cnthctl", dwarf::arm32::cnthctl, dwarf::arm32::cnthctl, 32, "int32"},
      register_mapping{"cnthv_tval", dwarf::arm32::cnthv_tval, dwarf::arm32::cnthv_tval, 32, "int32"},
      register_mapping{"cnthv_ctl", dwarf::arm32::cnthv_ctl, dwarf::arm32::cnthv_ctl, 32, "int32"},
      register_mapping{"cnthv_cval", dwarf::arm32::cnthv_cval, dwarf::arm32::cnthv_cval, 64, "int64"},
      register_mapping{"cnthps_tval", dwarf::arm32::cnthps_tval, dwarf::arm32::cnthps_tval, 32, "int32"},
      register_mapping{"cnthps_ctl", dwarf::arm32::cnthps_ctl, dwarf::arm32::cnthps_ctl, 32, "int32"},
      register_mapping{"cnthps_cval", dwarf::arm32::cnthps_cval, dwarf::arm32::cnthps_cval, 64, "int64"},
      register_mapping{"cnthvs_tval", dwarf::arm32::cnthvs_tval, dwarf::arm32::cnthvs_tval, 32, "int32"},
      register_mapping{"cnthvs_ctl", dwarf::arm32::cnthvs_ctl, dwarf::arm32::cnthvs_ctl, 32, "int32"},
      register_mapping{"cnthvs_cval", dwarf::arm32::cnthvs_cval, dwarf::arm32::cnthvs_cval, 64, "int64"},
      register_mapping{"cntkctl", dwarf::arm32::cntkctl, dwarf::arm32::cntkctl, 32, "int32"}};
};

// ============================================================================
// RISC-V 64
// ============================================================================
template <> struct register_traits<tags::riscv64> : register_traits_base<register_traits<tags::riscv64>> {
  static constexpr microfmt::array arch_layout{// GDB exactly matches DWARF for x0-x31
                                               register_mapping{"zero", 0, dwarf::riscv::x0, 64, "int64"},
                                               register_mapping{"ra", 1, dwarf::riscv::ra, 64, "code_ptr"},
                                               register_mapping{"sp", 2, dwarf::riscv::sp, 64, "data_ptr"},
                                               register_mapping{"gp", 3, dwarf::riscv::gp, 64, "data_ptr"},
                                               register_mapping{"tp", 4, dwarf::riscv::tp, 64, "data_ptr"},
                                               register_mapping{"t0", 5, dwarf::riscv::t0, 64, "int64"},
                                               register_mapping{"t1", 6, dwarf::riscv::t1, 64, "int64"},
                                               register_mapping{"t2", 7, dwarf::riscv::t2, 64, "int64"},
                                               register_mapping{"s0", 8, dwarf::riscv::s0, 64, "data_ptr"},
                                               register_mapping{"s1", 9, dwarf::riscv::s1, 64, "int64"},
                                               register_mapping{"a0", 10, dwarf::riscv::a0, 64, "int64"},
                                               register_mapping{"a1", 11, dwarf::riscv::a1, 64, "int64"},
                                               register_mapping{"a2", 12, dwarf::riscv::a2, 64, "int64"},
                                               register_mapping{"a3", 13, dwarf::riscv::a3, 64, "int64"},
                                               register_mapping{"a4", 14, dwarf::riscv::a4, 64, "int64"},
                                               register_mapping{"a5", 15, dwarf::riscv::a5, 64, "int64"},
                                               register_mapping{"a6", 16, dwarf::riscv::a6, 64, "int64"},
                                               register_mapping{"a7", 17, dwarf::riscv::a7, 64, "int64"},
                                               register_mapping{"s2", 18, dwarf::riscv::s2, 64, "int64"},
                                               register_mapping{"s3", 19, dwarf::riscv::s3, 64, "int64"},
                                               register_mapping{"s4", 20, dwarf::riscv::s4, 64, "int64"},
                                               register_mapping{"s5", 21, dwarf::riscv::s5, 64, "int64"},
                                               register_mapping{"s6", 22, dwarf::riscv::s6, 64, "int64"},
                                               register_mapping{"s7", 23, dwarf::riscv::s7, 64, "int64"},
                                               register_mapping{"s8", 24, dwarf::riscv::s8, 64, "int64"},
                                               register_mapping{"s9", 25, dwarf::riscv::s9, 64, "int64"},
                                               register_mapping{"s10", 26, dwarf::riscv::s10, 64, "int64"},
                                               register_mapping{"s11", 27, dwarf::riscv::s11, 64, "int64"},
                                               register_mapping{"t3", 28, dwarf::riscv::t3, 64, "int64"},
                                               register_mapping{"t4", 29, dwarf::riscv::t4, 64, "int64"},
                                               register_mapping{"t5", 30, dwarf::riscv::t5, 64, "int64"},
                                               register_mapping{"t6", 31, dwarf::riscv::t6, 64, "int64"},

                                               // Divergence: GDB PC is 32, DWARF PC is 65
                                               register_mapping{"pc", 32, dwarf::riscv::pc, 64, "code_ptr"}};

  static constexpr microfmt::array extended_arch_layout{
      register_mapping{"f0", 33, dwarf::riscv::f0, 64, "ieee_double"},
      register_mapping{"f1", 34, dwarf::riscv::f1, 64, "ieee_double"},
      register_mapping{"f2", 35, dwarf::riscv::f2, 64, "ieee_double"},
      register_mapping{"f3", 36, dwarf::riscv::f3, 64, "ieee_double"},
      register_mapping{"f4", 37, dwarf::riscv::f4, 64, "ieee_double"},
      register_mapping{"f5", 38, dwarf::riscv::f5, 64, "ieee_double"},
      register_mapping{"f6", 39, dwarf::riscv::f6, 64, "ieee_double"},
      register_mapping{"f7", 40, dwarf::riscv::f7, 64, "ieee_double"},
      register_mapping{"f8", 41, dwarf::riscv::f8, 64, "ieee_double"},
      register_mapping{"f9", 42, dwarf::riscv::f9, 64, "ieee_double"},
      register_mapping{"f10", 43, dwarf::riscv::f10, 64, "ieee_double"},
      register_mapping{"f11", 44, dwarf::riscv::f11, 64, "ieee_double"},
      register_mapping{"f12", 45, dwarf::riscv::f12, 64, "ieee_double"},
      register_mapping{"f13", 46, dwarf::riscv::f13, 64, "ieee_double"},
      register_mapping{"f14", 47, dwarf::riscv::f14, 64, "ieee_double"},
      register_mapping{"f15", 48, dwarf::riscv::f15, 64, "ieee_double"},
      register_mapping{"f16", 49, dwarf::riscv::f16, 64, "ieee_double"},
      register_mapping{"f17", 50, dwarf::riscv::f17, 64, "ieee_double"},
      register_mapping{"f18", 51, dwarf::riscv::f18, 64, "ieee_double"},
      register_mapping{"f19", 52, dwarf::riscv::f19, 64, "ieee_double"},
      register_mapping{"f20", 53, dwarf::riscv::f20, 64, "ieee_double"},
      register_mapping{"f21", 54, dwarf::riscv::f21, 64, "ieee_double"},
      register_mapping{"f22", 55, dwarf::riscv::f22, 64, "ieee_double"},
      register_mapping{"f23", 56, dwarf::riscv::f23, 64, "ieee_double"},
      register_mapping{"f24", 57, dwarf::riscv::f24, 64, "ieee_double"},
      register_mapping{"f25", 58, dwarf::riscv::f25, 64, "ieee_double"},
      register_mapping{"f26", 59, dwarf::riscv::f26, 64, "ieee_double"},
      register_mapping{"f27", 60, dwarf::riscv::f27, 64, "ieee_double"},
      register_mapping{"f28", 61, dwarf::riscv::f28, 64, "ieee_double"},
      register_mapping{"f29", 62, dwarf::riscv::f29, 64, "ieee_double"},
      register_mapping{"f30", 63, dwarf::riscv::f30, 64, "ieee_double"},
      register_mapping{"f31", 64, dwarf::riscv::f31, 64, "ieee_double"}};

  static constexpr microfmt::array non_standard_arch_layout{
      register_mapping{"sstatus", dwarf::riscv::sstatus, dwarf::riscv::sstatus, 64, "int64"},
      register_mapping{"sepc", dwarf::riscv::sepc, dwarf::riscv::sepc, 64, "code_ptr"},
      register_mapping{"stval", dwarf::riscv::stval, dwarf::riscv::stval, 64, "data_ptr"},
      register_mapping{"satp", dwarf::riscv::satp, dwarf::riscv::satp, 64, "data_ptr"},
      register_mapping{"mstatus", dwarf::riscv::mstatus, dwarf::riscv::mstatus, 64, "int64"},
      register_mapping{"mepc", dwarf::riscv::mepc, dwarf::riscv::mepc, 64, "code_ptr"},
      register_mapping{"mtvec", dwarf::riscv::mtvec, dwarf::riscv::mtvec, 64, "code_ptr"}};
};

// ============================================================================
// RISC-V 32
// ============================================================================
template <> struct register_traits<tags::riscv32> : register_traits_base<register_traits<tags::riscv32>> {
  static constexpr microfmt::array arch_layout{register_mapping{"zero", 0, dwarf::riscv::x0, 32, "int32"},
                                               register_mapping{"ra", 1, dwarf::riscv::ra, 32, "code_ptr"},
                                               register_mapping{"sp", 2, dwarf::riscv::sp, 32, "data_ptr"},
                                               register_mapping{"gp", 3, dwarf::riscv::gp, 32, "data_ptr"},
                                               register_mapping{"tp", 4, dwarf::riscv::tp, 32, "data_ptr"},
                                               register_mapping{"t0", 5, dwarf::riscv::t0, 32, "int32"},
                                               register_mapping{"t1", 6, dwarf::riscv::t1, 32, "int32"},
                                               register_mapping{"t2", 7, dwarf::riscv::t2, 32, "int32"},
                                               register_mapping{"s0", 8, dwarf::riscv::s0, 32, "data_ptr"},
                                               register_mapping{"s1", 9, dwarf::riscv::s1, 32, "int32"},
                                               register_mapping{"a0", 10, dwarf::riscv::a0, 32, "int32"},
                                               register_mapping{"a1", 11, dwarf::riscv::a1, 32, "int32"},
                                               register_mapping{"a2", 12, dwarf::riscv::a2, 32, "int32"},
                                               register_mapping{"a3", 13, dwarf::riscv::a3, 32, "int32"},
                                               register_mapping{"a4", 14, dwarf::riscv::a4, 32, "int32"},
                                               register_mapping{"a5", 15, dwarf::riscv::a5, 32, "int32"},
                                               register_mapping{"a6", 16, dwarf::riscv::a6, 32, "int32"},
                                               register_mapping{"a7", 17, dwarf::riscv::a7, 32, "int32"},
                                               register_mapping{"s2", 18, dwarf::riscv::s2, 32, "int32"},
                                               register_mapping{"s3", 19, dwarf::riscv::s3, 32, "int32"},
                                               register_mapping{"s4", 20, dwarf::riscv::s4, 32, "int32"},
                                               register_mapping{"s5", 21, dwarf::riscv::s5, 32, "int32"},
                                               register_mapping{"s6", 22, dwarf::riscv::s6, 32, "int32"},
                                               register_mapping{"s7", 23, dwarf::riscv::s7, 32, "int32"},
                                               register_mapping{"s8", 24, dwarf::riscv::s8, 32, "int32"},
                                               register_mapping{"s9", 25, dwarf::riscv::s9, 32, "int32"},
                                               register_mapping{"s10", 26, dwarf::riscv::s10, 32, "int32"},
                                               register_mapping{"s11", 27, dwarf::riscv::s11, 32, "int32"},
                                               register_mapping{"t3", 28, dwarf::riscv::t3, 32, "int32"},
                                               register_mapping{"t4", 29, dwarf::riscv::t4, 32, "int32"},
                                               register_mapping{"t5", 30, dwarf::riscv::t5, 32, "int32"},
                                               register_mapping{"t6", 31, dwarf::riscv::t6, 32, "int32"},
                                               register_mapping{"pc", 32, dwarf::riscv::pc, 32, "code_ptr"}};

  static constexpr microfmt::array extended_arch_layout{
      register_mapping{"f0", 33, dwarf::riscv::f0, 32, "ieee_single"},
      register_mapping{"f1", 34, dwarf::riscv::f1, 32, "ieee_single"},
      register_mapping{"f2", 35, dwarf::riscv::f2, 32, "ieee_single"},
      register_mapping{"f3", 36, dwarf::riscv::f3, 32, "ieee_single"},
      register_mapping{"f4", 37, dwarf::riscv::f4, 32, "ieee_single"},
      register_mapping{"f5", 38, dwarf::riscv::f5, 32, "ieee_single"},
      register_mapping{"f6", 39, dwarf::riscv::f6, 32, "ieee_single"},
      register_mapping{"f7", 40, dwarf::riscv::f7, 32, "ieee_single"},
      register_mapping{"f8", 41, dwarf::riscv::f8, 32, "ieee_single"},
      register_mapping{"f9", 42, dwarf::riscv::f9, 32, "ieee_single"},
      register_mapping{"f10", 43, dwarf::riscv::f10, 32, "ieee_single"},
      register_mapping{"f11", 44, dwarf::riscv::f11, 32, "ieee_single"},
      register_mapping{"f12", 45, dwarf::riscv::f12, 32, "ieee_single"},
      register_mapping{"f13", 46, dwarf::riscv::f13, 32, "ieee_single"},
      register_mapping{"f14", 47, dwarf::riscv::f14, 32, "ieee_single"},
      register_mapping{"f15", 48, dwarf::riscv::f15, 32, "ieee_single"},
      register_mapping{"f16", 49, dwarf::riscv::f16, 32, "ieee_single"},
      register_mapping{"f17", 50, dwarf::riscv::f17, 32, "ieee_single"},
      register_mapping{"f18", 51, dwarf::riscv::f18, 32, "ieee_single"},
      register_mapping{"f19", 52, dwarf::riscv::f19, 32, "ieee_single"},
      register_mapping{"f20", 53, dwarf::riscv::f20, 32, "ieee_single"},
      register_mapping{"f21", 54, dwarf::riscv::f21, 32, "ieee_single"},
      register_mapping{"f22", 55, dwarf::riscv::f22, 32, "ieee_single"},
      register_mapping{"f23", 56, dwarf::riscv::f23, 32, "ieee_single"},
      register_mapping{"f24", 57, dwarf::riscv::f24, 32, "ieee_single"},
      register_mapping{"f25", 58, dwarf::riscv::f25, 32, "ieee_single"},
      register_mapping{"f26", 59, dwarf::riscv::f26, 32, "ieee_single"},
      register_mapping{"f27", 60, dwarf::riscv::f27, 32, "ieee_single"},
      register_mapping{"f28", 61, dwarf::riscv::f28, 32, "ieee_single"},
      register_mapping{"f29", 62, dwarf::riscv::f29, 32, "ieee_single"},
      register_mapping{"f30", 63, dwarf::riscv::f30, 32, "ieee_single"},
      register_mapping{"f31", 64, dwarf::riscv::f31, 32, "ieee_single"}};

  static constexpr microfmt::array non_standard_arch_layout{
      register_mapping{"sstatus", dwarf::riscv::sstatus, dwarf::riscv::sstatus, 32, "int32"},
      register_mapping{"sepc", dwarf::riscv::sepc, dwarf::riscv::sepc, 32, "code_ptr"},
      register_mapping{"stval", dwarf::riscv::stval, dwarf::riscv::stval, 32, "data_ptr"},
      register_mapping{"satp", dwarf::riscv::satp, dwarf::riscv::satp, 32, "data_ptr"},
      register_mapping{"mstatus", dwarf::riscv::mstatus, dwarf::riscv::mstatus, 32, "int32"},
      register_mapping{"mepc", dwarf::riscv::mepc, dwarf::riscv::mepc, 32, "code_ptr"},
      register_mapping{"mtvec", dwarf::riscv::mtvec, dwarf::riscv::mtvec, 32, "code_ptr"}};
};

} // namespace microfmt::gdb