// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file dwarf_abi.hpp @brief Per-architecture ABI traits describing registers
 * used during DWARF unwinding. */

#include "dwarf_registers.hpp"
#include <cstddef>
#include <cstdint>

namespace microfmt {

// ============================================================================
// ARM / Thumb (32-bit EABI)
// ============================================================================

/**
 * @brief DWARF unwind ABI traits for ARM / Thumb (32-bit EABI).
 */
struct arm_abi_traits {
  /// Register value width.
  using register_type = uint32_t;
  /// Architecture register name/index catalog.
  using register_traits = dwarf::arm32::register_traits;
  /// Pointer size in bytes.
  static constexpr size_t pointer_size = 4;

  /// SP register number.
  static constexpr uint32_t sp_reg = dwarf::arm32::SP; // R13
  /// LR register number.
  static constexpr uint32_t lr_reg = dwarf::arm32::LR; // R14
  /// Return-address register number.
  static constexpr uint32_t ra_reg = lr_reg;
  /// FP register number.
  static constexpr uint32_t fp_reg = dwarf::arm32::FP; // R11 (Traditional ARM)

  // Frame pointer layout offsets relative to current FP
  static constexpr ptrdiff_t fp_slot_offset = 0; // Saved FP is at [FP + 0]
  static constexpr ptrdiff_t ra_slot_offset = 4; // Saved LR is at [FP + 4]

  /**
   * @brief Reports whether a register holds the return address.
   * @param reg DWARF register number.
   * @return `true` for the link register (R14).
   */
  [[nodiscard]] static constexpr bool
  is_return_address_register(uint32_t reg) noexcept {
    return reg == lr_reg;
  }
  /**
   * @brief Reports whether a register is a frame pointer.
   * @param reg DWARF register number.
   * @return `true` for R11 and R7 (Thumb frame pointer).
   */
  [[nodiscard]] static constexpr bool
  is_frame_pointer_register(uint32_t reg) noexcept {
    return reg == fp_reg ||
           reg == 7; // Support both R11 and R7 (Thumb frame pointer)
  }

  [[nodiscard]] static constexpr uintptr_t
  normalize_pc(uintptr_t raw_ra) noexcept {
    // Clear the Thumb-2 mode bit (bit 0)
    return raw_ra & ~static_cast<uintptr_t>(1);
  }
};

// ============================================================================
// AArch64 (64-bit ARM)
// ============================================================================

/**
 * @brief DWARF unwind ABI traits for AArch64 (64-bit ARM).
 */
struct aarch64_abi_traits {
  /// Register value width.
  using register_type = uint64_t;
  /// Architecture register name/index catalog.
  using register_traits = dwarf::aarch64::register_traits;
  /// Pointer size in bytes.
  static constexpr size_t pointer_size = 8;

  /// SP register number.
  static constexpr uint32_t sp_reg = dwarf::aarch64::SP; // X31 / SP
  /// RA/LR register number.
  static constexpr uint32_t ra_reg = dwarf::aarch64::LR; // X30 (LR)
  /// FP register number.
  static constexpr uint32_t fp_reg = dwarf::aarch64::FP; // X29 (FP)

  static constexpr ptrdiff_t fp_slot_offset = 0; // Saved FP is at [FP + 0]
  static constexpr ptrdiff_t ra_slot_offset = 8; // Saved LR is at [FP + 8]

  /**
   * @brief Reports whether a register holds the return address.
   * @param reg DWARF register number.
   * @return `true` for the link register (X30).
   */
  [[nodiscard]] static constexpr bool
  is_return_address_register(uint32_t reg) noexcept {
    return reg == ra_reg;
  }
  /**
   * @brief Reports whether a register is a frame pointer.
   * @param reg DWARF register number.
   * @return `true` for X29.
   */
  [[nodiscard]] static constexpr bool
  is_frame_pointer_register(uint32_t reg) noexcept {
    return reg == fp_reg;
  }

  [[nodiscard]] static constexpr uintptr_t
  normalize_pc(uintptr_t raw_ra) noexcept {
    if constexpr (sizeof(uintptr_t) < sizeof(uint64_t))
      return raw_ra & ~static_cast<uintptr_t>(1);

    const uint64_t pc = static_cast<uint64_t>(raw_ra) & ~UINT64_C(1);

    // Check if it's a kernel-space address (higher-half check: top bit set)
    const bool is_kernel = (pc & (UINT64_C(1) << 63)) != 0;

    if (is_kernel) {
      return static_cast<uintptr_t>((pc & UINT64_C(0x0000FFFFFFFFFFFF)) |
                                    UINT64_C(0xFFFF000000000000));
    } else {
      // User-space PAC masking
      return static_cast<uintptr_t>(pc & UINT64_C(0x0000FFFFFFFFFFFF));
    }
  }
};

// ============================================================================
// RISC-V 32-bit (RV32)
// ============================================================================

/**
 * @brief DWARF unwind ABI traits for RISC-V 32-bit (RV32).
 */
struct riscv32_abi_traits {
  /// Register value width.
  using register_type = uint32_t;
  /// Architecture register name/index catalog.
  using register_traits = dwarf::riscv::register_traits;
  /// Pointer size in bytes.
  static constexpr size_t pointer_size = 4;

  /// SP register number.
  static constexpr uint32_t sp_reg = dwarf::riscv::SP; // x2
  /// RA register number.
  static constexpr uint32_t ra_reg = dwarf::riscv::RA; // x1
  /// FP register number.
  static constexpr uint32_t fp_reg = dwarf::riscv::FP; // x8 (s0/fp)

  // Standard RISC-V convention (saves s0/fp and ra in the frame):
  // [FP + 0] -> saved frame pointer (s0 / x8)
  // [FP + 4] -> saved return address (ra / x1)
  static constexpr ptrdiff_t fp_slot_offset = 0;
  static constexpr ptrdiff_t ra_slot_offset = 4;

  /**
   * @brief Reports whether a register holds the return address.
   * @param reg DWARF register number.
   * @return `true` for x1 (ra).
   */
  [[nodiscard]] static constexpr bool
  is_return_address_register(uint32_t reg) noexcept {
    return reg == ra_reg;
  }
  /**
   * @brief Reports whether a register is a frame pointer.
   * @param reg DWARF register number.
   * @return `true` for x8 (s0/fp).
   */
  [[nodiscard]] static constexpr bool
  is_frame_pointer_register(uint32_t reg) noexcept {
    return reg == fp_reg;
  }

  [[nodiscard]] static constexpr uintptr_t
  normalize_pc(uintptr_t raw_ra) noexcept {
    return raw_ra;
  }
};

// ============================================================================
// RISC-V 64-bit (RV64)
// ============================================================================

/**
 * @brief DWARF unwind ABI traits for RISC-V 64-bit (RV64).
 */
struct riscv64_abi_traits {
  /// Register value width.
  using register_type = uint64_t;
  /// Architecture register name/index catalog.
  using register_traits = dwarf::riscv::register_traits;
  /// Pointer size in bytes.
  static constexpr size_t pointer_size = 8;

  /// SP register number.
  static constexpr uint32_t sp_reg = dwarf::riscv::SP; // x2
  /// RA register number.
  static constexpr uint32_t ra_reg = dwarf::riscv::RA; // x1
  /// FP register number.
  static constexpr uint32_t fp_reg = dwarf::riscv::FP; // x8 (s0/fp)

  static constexpr ptrdiff_t fp_slot_offset = 0; // Saved FP (s0) is at [FP + 0]
  static constexpr ptrdiff_t ra_slot_offset = 8; // Saved RA is at [FP + 8]

  /**
   * @brief Reports whether a register holds the return address.
   * @param reg DWARF register number.
   * @return `true` for x1 (ra).
   */
  [[nodiscard]] static constexpr bool
  is_return_address_register(uint32_t reg) noexcept {
    return reg == ra_reg;
  }
  /**
   * @brief Reports whether a register is a frame pointer.
   * @param reg DWARF register number.
   * @return `true` for x8 (s0/fp).
   */
  [[nodiscard]] static constexpr bool
  is_frame_pointer_register(uint32_t reg) noexcept {
    return reg == fp_reg;
  }

  [[nodiscard]] static constexpr uintptr_t
  normalize_pc(uintptr_t raw_ra) noexcept {
    return raw_ra;
  }
};

// ============================================================================
// x86 (32-bit IA-32)
// ============================================================================

/**
 * @brief DWARF unwind ABI traits for x86 (32-bit IA-32).
 */
struct x86_abi_traits {
  /// Register value width.
  using register_type = uint32_t;
  /// Architecture register name/index catalog.
  using register_traits = dwarf::x86::register_traits;
  /// Pointer size in bytes.
  static constexpr size_t pointer_size = 4;

  /// SP register number.
  static constexpr uint32_t sp_reg = dwarf::x86::SP; // ESP (Register 4)
  /// RA register number.
  static constexpr uint32_t ra_reg = dwarf::x86::PC; // EIP (Register 8)
  /// FP register number.
  static constexpr uint32_t fp_reg = dwarf::x86::FP; // EBP (Register 5)

  // On 32-bit x86:
  // [EBP + 0] -> stores the saved previous EBP (Frame Pointer)
  // [EBP + 4] -> stores the return address (EIP)
  static constexpr ptrdiff_t fp_slot_offset = 0;
  static constexpr ptrdiff_t ra_slot_offset = 4;

  /**
   * @brief Reports whether a register holds the return address.
   * @param reg DWARF register number.
   * @return `true` for EIP (register 8).
   */
  [[nodiscard]] static constexpr bool
  is_return_address_register(uint32_t reg) noexcept {
    return reg == ra_reg;
  }
  /**
   * @brief Reports whether a register is a frame pointer.
   * @param reg DWARF register number.
   * @return `true` for EBP.
   */
  [[nodiscard]] static constexpr bool
  is_frame_pointer_register(uint32_t reg) noexcept {
    return reg == fp_reg;
  }

  [[nodiscard]] static constexpr uintptr_t
  normalize_pc(uintptr_t raw_ra) noexcept {
    return raw_ra;
  }
};

// ============================================================================
// x86-64 (AMD64 / x86_64)
// ============================================================================

/**
 * @brief DWARF unwind ABI traits for x86-64 (AMD64 / x86_64).
 */
struct x86_64_abi_traits {
  /// Register value width.
  using register_type = uint64_t;
  /// Architecture register name/index catalog.
  using register_traits = dwarf::x86_64::register_traits;
  /// Pointer size in bytes.
  static constexpr size_t pointer_size = 8;

  /// SP register number.
  static constexpr uint32_t sp_reg = dwarf::x86_64::SP; // RSP (Register 7)
  /// RA register number.
  static constexpr uint32_t ra_reg = dwarf::x86_64::PC; // RIP (Register 16)
  /// FP register number.
  static constexpr uint32_t fp_reg = dwarf::x86_64::FP; // RBP (Register 6)

  static constexpr ptrdiff_t fp_slot_offset = 0; // Saved RBP is at [RBP + 0]
  static constexpr ptrdiff_t ra_slot_offset = 8; // Saved RIP is at [RBP + 8]

  /**
   * @brief Reports whether a register holds the return address.
   * @param reg DWARF register number.
   * @return `true` for RIP (register 16).
   */
  [[nodiscard]] static constexpr bool
  is_return_address_register(uint32_t reg) noexcept {
    return reg == ra_reg;
  }
  /**
   * @brief Reports whether a register is a frame pointer.
   * @param reg DWARF register number.
   * @return `true` for RBP.
   */
  [[nodiscard]] static constexpr bool
  is_frame_pointer_register(uint32_t reg) noexcept {
    return reg == fp_reg;
  }

  [[nodiscard]] static constexpr uintptr_t
  normalize_pc(uintptr_t raw_ra) noexcept {
    return raw_ra;
  }
};

template <typename Traits> struct validate_abi_traits {
  static_assert(
      sizeof(typename Traits::register_type) == Traits::pointer_size,
      "AbiTraits error: register_type size must exactly match pointer_size!");

  static_assert(
      Traits::pointer_size == 4 || Traits::pointer_size == 8,
      "AbiTraits error: pointer_size must be either 4 (32-bit) or 8 (64-bit)!");

  static constexpr bool value = true;
};

// Enforce validation checks for all built-in traits at compile time
static_assert(validate_abi_traits<arm_abi_traits>::value, "");
static_assert(validate_abi_traits<aarch64_abi_traits>::value, "");
static_assert(validate_abi_traits<riscv32_abi_traits>::value, "");
static_assert(validate_abi_traits<riscv64_abi_traits>::value, "");
static_assert(validate_abi_traits<x86_abi_traits>::value, "");
static_assert(validate_abi_traits<x86_64_abi_traits>::value, "");

} // namespace microfmt