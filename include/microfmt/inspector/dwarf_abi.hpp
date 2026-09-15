#pragma once

/** @file dwarf_abi.hpp @brief Per-architecture ABI traits describing registers
 * used during DWARF unwinding. */

#include <cstddef>
#include <cstdint>

namespace microfmt {

// ============================================================================
// ARM / Thumb (32-bit EABI)
// ============================================================================
struct arm_abi_traits {
  using register_type = uint32_t;
  static constexpr size_t pointer_size = 4;

  static constexpr uint32_t sp_reg = 13; // R13 (SP)
  static constexpr uint32_t lr_reg = 14; // R14 (LR)
  static constexpr uint32_t fp_reg =
      11; // R11 (Traditional ARM; note: some thumb variants use R7 = 7)

  [[nodiscard]] static constexpr bool
  is_return_address_register(uint32_t reg) noexcept {
    return reg == lr_reg;
  }
  [[nodiscard]] static constexpr bool
  is_frame_pointer_register(uint32_t reg) noexcept {
    return reg == fp_reg ||
           reg == 7; // Support both R11 and R7 (Thumb frame pointer)
  }
};

// ============================================================================
// AArch64 (64-bit ARM)
// ============================================================================
struct aarch64_abi_traits {
  using register_type = uint64_t;
  static constexpr size_t pointer_size = 8;

  static constexpr uint32_t sp_reg = 31; // X31 / SP
  static constexpr uint32_t ra_reg = 30; // X30 (LR / Link Register)
  static constexpr uint32_t fp_reg = 29; // X29 (FP / Frame Pointer)

  [[nodiscard]] static constexpr bool
  is_return_address_register(uint32_t reg) noexcept {
    return reg == ra_reg;
  }
  [[nodiscard]] static constexpr bool
  is_frame_pointer_register(uint32_t reg) noexcept {
    return reg == fp_reg;
  }
};

// ============================================================================
// RISC-V 32-bit (RV32)
// ============================================================================
struct riscv32_abi_traits {
  using register_type = uint32_t;
  static constexpr size_t pointer_size = 4;

  static constexpr uint32_t sp_reg = 2;
  static constexpr uint32_t ra_reg = 1;
  static constexpr uint32_t fp_reg = 8;

  [[nodiscard]] static constexpr bool
  is_return_address_register(uint32_t reg) noexcept {
    return reg == ra_reg;
  }
  [[nodiscard]] static constexpr bool
  is_frame_pointer_register(uint32_t reg) noexcept {
    return reg == fp_reg;
  }
};

// ============================================================================
// RISC-V 64-bit (RV64)
// ============================================================================
struct riscv64_abi_traits {
  using register_type = uint64_t;
  static constexpr size_t pointer_size = 8;

  static constexpr uint32_t sp_reg = 2;
  static constexpr uint32_t ra_reg = 1;
  static constexpr uint32_t fp_reg = 8;

  [[nodiscard]] static constexpr bool
  is_return_address_register(uint32_t reg) noexcept {
    return reg == ra_reg;
  }
  [[nodiscard]] static constexpr bool
  is_frame_pointer_register(uint32_t reg) noexcept {
    return reg == fp_reg;
  }
};

// ============================================================================
// x86 (32-bit IA-32)
// ============================================================================
struct x86_abi_traits {
  using register_type = uint32_t;
  static constexpr size_t pointer_size = 4;

  static constexpr uint32_t sp_reg = 4; // ESP (Register 4)
  static constexpr uint32_t ra_reg =
      8; // EIP (Register 8 - Return address slot)
  static constexpr uint32_t fp_reg = 5; // EBP (Register 5)

  [[nodiscard]] static constexpr bool
  is_return_address_register(uint32_t reg) noexcept {
    return reg == ra_reg;
  }
  [[nodiscard]] static constexpr bool
  is_frame_pointer_register(uint32_t reg) noexcept {
    return reg == fp_reg;
  }
};

// ============================================================================
// x86-64 (AMD64 / x86_64)
// ============================================================================
struct x86_64_abi_traits {
  using register_type = uint64_t;
  static constexpr size_t pointer_size = 8;

  static constexpr uint32_t sp_reg = 7; // RSP (Register 7)
  static constexpr uint32_t ra_reg =
      16; // RIP (Register 16 - Return address slot)
  static constexpr uint32_t fp_reg = 6; // RBP (Register 6)

  [[nodiscard]] static constexpr bool
  is_return_address_register(uint32_t reg) noexcept {
    return reg == ra_reg;
  }
  [[nodiscard]] static constexpr bool
  is_frame_pointer_register(uint32_t reg) noexcept {
    return reg == fp_reg;
  }
};

} // namespace microfmt