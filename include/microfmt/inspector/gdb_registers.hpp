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
  string_view gdb_type; // "int", "code_ptr", "data_ptr", "float"
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
    return nullptr;
  }

  [[nodiscard]] static constexpr const register_mapping *find_by_dwarf(uint32_t index) noexcept {
    for (const auto &reg : Traits::arch_layout) {
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
    return nullptr;
  }

  [[nodiscard]] static constexpr span<const register_mapping> layout() noexcept {
    return span<const register_mapping>{Traits::arch_layout.data(), Traits::arch_layout.size()};
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
template <>
struct register_traits<tags::x86_64>
    : register_traits_base<register_traits<tags::x86_64>> {
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
};

// ============================================================================
// x86 (IA-32)
// ============================================================================
template <>
struct register_traits<tags::x86>
    : register_traits_base<register_traits<tags::x86>> {
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
};

// ============================================================================
// AArch64 (ARM 64-bit)
// ============================================================================
template <>
struct register_traits<tags::aarch64>
    : register_traits_base<register_traits<tags::aarch64>> {
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
};

// ============================================================================
// ARM32 (AArch32)
// ============================================================================
template <>
struct register_traits<tags::arm32>
    : register_traits_base<register_traits<tags::arm32>> {
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
};

// ============================================================================
// RISC-V 64
// ============================================================================
template <>
struct register_traits<tags::riscv64>
    : register_traits_base<register_traits<tags::riscv64>> {
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
};

// ============================================================================
// RISC-V 32
// ============================================================================
template <>
struct register_traits<tags::riscv32>
    : register_traits_base<register_traits<tags::riscv32>> {
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
};

} // namespace microfmt::gdb