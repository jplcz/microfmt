#pragma once

/** @file dwarf_abi.hpp @brief Per-architecture ABI traits describing registers
 * used during DWARF unwinding. */

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
  /// Pointer size in bytes.
  static constexpr size_t pointer_size = 4;

  /// R13 (SP) register number.
  static constexpr uint32_t sp_reg = 13; // R13 (SP)
  /// R14 (LR) register number.
  static constexpr uint32_t lr_reg = 14; // R14 (LR)
  /// R11 (FP) register number.
  static constexpr uint32_t fp_reg =
      11; // R11 (Traditional ARM; note: some thumb variants use R7 = 7)

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
  /// Pointer size in bytes.
  static constexpr size_t pointer_size = 8;

  /// X31 (SP) register number.
  static constexpr uint32_t sp_reg = 31; // X31 / SP
  /// X30 (LR) register number.
  static constexpr uint32_t ra_reg = 30; // X30 (LR / Link Register)
  /// X29 (FP) register number.
  static constexpr uint32_t fp_reg = 29; // X29 (FP / Frame Pointer)

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
  /// Pointer size in bytes.
  static constexpr size_t pointer_size = 4;

  /// x2 (sp) register number.
  static constexpr uint32_t sp_reg = 2;
  /// x1 (ra) register number.
  static constexpr uint32_t ra_reg = 1;
  /// x8 (s0/fp) register number.
  static constexpr uint32_t fp_reg = 8;

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
  /// Pointer size in bytes.
  static constexpr size_t pointer_size = 8;

  /// x2 (sp) register number.
  static constexpr uint32_t sp_reg = 2;
  /// x1 (ra) register number.
  static constexpr uint32_t ra_reg = 1;
  /// x8 (s0/fp) register number.
  static constexpr uint32_t fp_reg = 8;

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
  /// Pointer size in bytes.
  static constexpr size_t pointer_size = 4;

  /// ESP (register 4) number.
  static constexpr uint32_t sp_reg = 4; // ESP (Register 4)
  /// EIP (register 8, return address slot) number.
  static constexpr uint32_t ra_reg =
      8; // EIP (Register 8 - Return address slot)
  /// EBP (register 5) number.
  static constexpr uint32_t fp_reg = 5; // EBP (Register 5)

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
  /// Pointer size in bytes.
  static constexpr size_t pointer_size = 8;

  /// RSP (register 7) number.
  static constexpr uint32_t sp_reg = 7; // RSP (Register 7)
  /// RIP (register 16, return address slot) number.
  static constexpr uint32_t ra_reg =
      16; // RIP (Register 16 - Return address slot)
  /// RBP (register 6) number.
  static constexpr uint32_t fp_reg = 6; // RBP (Register 6)

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
};

} // namespace microfmt