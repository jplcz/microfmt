// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file dwarf_abi.hpp @brief Per-architecture ABI traits describing registers
 * used during DWARF unwinding. */

#include "dwarf_registers.hpp"
#include "gdb_registers.hpp"
#include "register_context.hpp"
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

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
  /// GDB-to-DWARF register mapping.
  using gdb_register_traits = gdb::register_traits<gdb::tags::arm32>;
  /// Pointer size in bytes.
  static constexpr size_t pointer_size = 4;

  /// SP register number.
  static constexpr uint32_t sp_reg = dwarf::arm32::sp; // R13
  /// LR register number.
  static constexpr uint32_t lr_reg = dwarf::arm32::lr; // R14
  /// Return-address register number.
  static constexpr uint32_t ra_reg = lr_reg;
  /// FP register number.
  static constexpr uint32_t fp_reg = dwarf::arm32::fp; // R11 (Traditional ARM)

  // Frame pointer layout offsets relative to current FP, for @ref
  // fp_unwinder_tag's naive FP-chain walking (as opposed to the
  // EXIDX-bytecode-driven `arm_exidx_unwinder_tag`, which needs no static
  // offset assumption at all).
  //
  // These describe GCC's *ARM (A32)* `-O0` prologue convention only:
  // `push {..., fp, lr}` (any earlier callee-saved scratch registers, e.g.
  // r4, may also be pushed) followed by `add fp, sp, <#pushed-bytes-1..2
  // registers>`, which always lands `fp` exactly on the saved-LR word
  // (`fp+0`), with the saved caller FP immediately below it (`fp-4`) --
  // verified against real arm-linux-gnueabi-gcc output; this holds
  // regardless of how many extra registers are pushed alongside {fp, lr}
  // since fp/lr are always the last two (adjacent, ascending-register-number
  // order) entries of the push list.
  //
  // Thumb/Thumb-2 mode (R7 frame pointer) does NOT follow a fixed offset:
  // GCC emits `mov r7, sp` (or `add r7, sp, #0`) *after* allocating the
  // local-variable area when any extra scratch register is pushed alongside
  // {r7, lr}, so r7 ends up pointing at the locals base rather than at the
  // {r7, lr} pair, and the true offset then depends on the (per-function,
  // frame-size-dependent) local allocation size. There is no single constant
  // that describes this, so `fp_unwinder_tag<arm_abi_traits>` is reliable
  // only while executing in ARM (A32) state; treat Thumb-state fallback
  // results as unreliable (prefer `arm_exidx_unwinder_tag`, which decodes
  // the real bytecode/CFI instead of guessing a fixed layout).
  static constexpr ptrdiff_t fp_slot_offset = -4; // Saved FP is at [FP - 4]
  static constexpr ptrdiff_t ra_slot_offset = 0;  // Saved LR is at [FP + 0]

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

  /**
   * @brief Resolves which register actually holds the frame pointer for the
   * *current* instruction set state.
   *
   * Unlike every other supported architecture, ARM's frame-pointer register
   * is not fixed: GCC/Clang use R11 in ARM (A32) state but R7 in Thumb/Thumb-2
   * state. Consulting the static @ref fp_reg alone silently mis-decodes any
   * Thumb frame. The definitive source of truth is the Thumb bit (bit 5,
   * `0x20`) of CPSR; when the register context cannot supply CPSR (e.g. a
   * synthetic/simulated register file in a test), this falls back to @ref
   * fp_reg (A32/R11).
   *
   * @param reg_ctx Register context to consult for CPSR.
   * @return `7` (R7) in Thumb state, otherwise @ref fp_reg (`11`, R11).
   */
  [[nodiscard]] static uint32_t
  resolve_fp_reg(register_context_ref reg_ctx) noexcept {
    uint32_t cpsr = 0;
    if (reg_ctx && reg_ctx.read(dwarf::arm32::cpsr, cpsr)) {
      constexpr uint32_t thumb_bit = 0x20U; // CPSR.T
      return (cpsr & thumb_bit) != 0U ? 7U : fp_reg;
    }
    return fp_reg;
  }

  /**
   * @brief Resolves the FP-relative saved-FP/saved-RA slot offsets for the
   * *current* instruction-set state, for @ref fp_unwinder_tag's naive
   * FP-chain walking.
   *
   * In ARM (A32) state, `fp` always ends up pointing directly at the
   * saved-LR word regardless of how many other callee-saved scratch
   * registers (e.g. r4) are pushed alongside `{fp, lr}` -- `fp`/`lr` are
   * always the last two, adjacent entries of the push list -- so @ref
   * fp_slot_offset (`-4`) / @ref ra_slot_offset (`0`) hold universally.
   *
   * In Thumb/Thumb-2 state this is *not* true: GCC emits `mov r7, sp` (or
   * `add r7, sp, #0`) *after* allocating the local-variable area whenever any
   * extra scratch register is pushed alongside `{r7, lr}`, so `r7` ends up
   * pointing at the locals base rather than at the `{r7, lr}` pair, and the
   * true offset then varies per function with its (frame-size-dependent)
   * local allocation. There is no fixed constant that describes this, so
   * this reports failure in Thumb state rather than returning a
   * plausible-looking but wrong offset.
   *
   * @param reg_ctx Register context to consult for CPSR.
   * @param[out] out_fp_slot_offset Receives @ref fp_slot_offset when
   * successful.
   * @param[out] out_ra_slot_offset Receives @ref ra_slot_offset when
   * successful.
   * @return `true` in ARM (A32) state (or when CPSR cannot be read, matching
   * @ref resolve_fp_reg's fallback); `false` in Thumb/Thumb-2 state, where
   * FP-chain walking cannot be trusted.
   */
  [[nodiscard]] static bool
  resolve_frame_slot_offsets(register_context_ref reg_ctx,
                            ptrdiff_t &out_fp_slot_offset,
                            ptrdiff_t &out_ra_slot_offset) noexcept {
    uint32_t cpsr = 0;
    if (reg_ctx && reg_ctx.read(dwarf::arm32::cpsr, cpsr)) {
      constexpr uint32_t thumb_bit = 0x20U; // CPSR.T
      if ((cpsr & thumb_bit) != 0U)
        return false;
    }
    out_fp_slot_offset = fp_slot_offset;
    out_ra_slot_offset = ra_slot_offset;
    return true;
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
  /// GDB-to-DWARF register mapping.
  using gdb_register_traits = gdb::register_traits<gdb::tags::aarch64>;
  /// Pointer size in bytes.
  static constexpr size_t pointer_size = 8;

  /// SP register number.
  static constexpr uint32_t sp_reg = dwarf::aarch64::sp; // X31 / SP
  /// RA/LR register number.
  static constexpr uint32_t ra_reg = dwarf::aarch64::lr; // X30 (LR)
  /// FP register number.
  static constexpr uint32_t fp_reg = dwarf::aarch64::fp; // X29 (FP)

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
  /// GDB-to-DWARF register mapping.
  using gdb_register_traits = gdb::register_traits<gdb::tags::riscv32>;
  /// Pointer size in bytes.
  static constexpr size_t pointer_size = 4;

  /// SP register number.
  static constexpr uint32_t sp_reg = dwarf::riscv::sp; // x2
  /// RA register number.
  static constexpr uint32_t ra_reg = dwarf::riscv::ra; // x1
  /// FP register number.
  static constexpr uint32_t fp_reg = dwarf::riscv::fp; // x8 (s0/fp)

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
  /// GDB-to-DWARF register mapping.
  using gdb_register_traits = gdb::register_traits<gdb::tags::riscv64>;
  /// Pointer size in bytes.
  static constexpr size_t pointer_size = 8;

  /// SP register number.
  static constexpr uint32_t sp_reg = dwarf::riscv::sp; // x2
  /// RA register number.
  static constexpr uint32_t ra_reg = dwarf::riscv::ra; // x1
  /// FP register number.
  static constexpr uint32_t fp_reg = dwarf::riscv::fp; // x8 (s0/fp)

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
  /// GDB-to-DWARF register mapping.
  using gdb_register_traits = gdb::register_traits<gdb::tags::x86>;
  /// Pointer size in bytes.
  static constexpr size_t pointer_size = 4;

  /// SP register number.
  static constexpr uint32_t sp_reg = dwarf::x86::sp; // ESP (Register 4)
  /// RA register number.
  static constexpr uint32_t ra_reg = dwarf::x86::pc; // EIP (Register 8)
  /// FP register number.
  static constexpr uint32_t fp_reg = dwarf::x86::fp; // EBP (Register 5)

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
  /// GDB-to-DWARF register mapping.
  using gdb_register_traits = gdb::register_traits<gdb::tags::x86_64>;
  /// Pointer size in bytes.
  static constexpr size_t pointer_size = 8;

  /// SP register number.
  static constexpr uint32_t sp_reg = dwarf::x86_64::sp; // RSP (Register 7)
  /// RA register number.
  static constexpr uint32_t ra_reg = dwarf::x86_64::pc; // RIP (Register 16)
  /// FP register number.
  static constexpr uint32_t fp_reg = dwarf::x86_64::fp; // RBP (Register 6)

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

namespace detail {

/// Detects whether `AbiTraits` supplies a dynamic, register-context-aware
/// `resolve_fp_reg(register_context_ref)` (currently only @ref
/// arm_abi_traits, whose frame-pointer register depends on the live
/// ARM/Thumb instruction-set state).
template <typename AbiTraits, typename = void>
struct has_resolve_fp_reg : std::false_type {};

template <typename AbiTraits>
struct has_resolve_fp_reg<
    AbiTraits, std::void_t<decltype(AbiTraits::resolve_fp_reg(
                   std::declval<register_context_ref>()))>> : std::true_type {
};

/// Detects whether `AbiTraits` supplies a dynamic, register-context-aware
/// `resolve_frame_slot_offsets(register_context_ref, ptrdiff_t&,
/// ptrdiff_t&)` (currently only @ref arm_abi_traits, whose FP-chain slot
/// layout depends on the live ARM/Thumb instruction-set state and is only
/// reliable in ARM/A32 state).
template <typename AbiTraits, typename = void>
struct has_resolve_frame_slot_offsets : std::false_type {};

template <typename AbiTraits>
struct has_resolve_frame_slot_offsets<
    AbiTraits,
    std::void_t<decltype(AbiTraits::resolve_frame_slot_offsets(
        std::declval<register_context_ref>(), std::declval<ptrdiff_t &>(),
        std::declval<ptrdiff_t &>()))>> : std::true_type {};

} // namespace detail

/**
 * @brief Resolves the frame-pointer register to use for @p AbiTraits.
 *
 * Architectures whose frame-pointer register is fixed (all except ARM) fall
 * back to the static @c AbiTraits::fp_reg. ARM's frame-pointer register
 * instead depends on the live ARM/Thumb instruction-set state (R11 vs. R7),
 * so @ref arm_abi_traits::resolve_fp_reg consults CPSR (or a documented
 * fallback) via @p reg_ctx.
 *
 * @tparam AbiTraits Architecture ABI traits.
 * @param reg_ctx Register context to consult, if the traits need it.
 * @return DWARF register number holding the frame pointer.
 */
template <typename AbiTraits>
[[nodiscard]] uint32_t
resolve_fp_register(register_context_ref reg_ctx) noexcept {
  if constexpr (detail::has_resolve_fp_reg<AbiTraits>::value) {
    return AbiTraits::resolve_fp_reg(reg_ctx);
  } else {
    return AbiTraits::fp_reg;
  }
}

/**
 * @brief Resolves the FP-relative saved-FP/saved-RA slot offsets to use for
 * @p AbiTraits's naive FP-chain walking (@ref fp_unwinder_tag).
 *
 * Architectures whose FP-chain layout is fixed (all except ARM) fall back to
 * the static @c AbiTraits::fp_slot_offset / @c AbiTraits::ra_slot_offset and
 * always succeed. ARM's layout instead depends on the live ARM/Thumb
 * instruction-set state -- reliable only in ARM (A32) state -- so @ref
 * arm_abi_traits::resolve_frame_slot_offsets consults CPSR via @p reg_ctx and
 * reports failure in Thumb/Thumb-2 state rather than guessing.
 *
 * @tparam AbiTraits Architecture ABI traits.
 * @param reg_ctx Register context to consult, if the traits need it.
 * @param[out] out_fp_slot_offset Receives the saved-FP slot offset.
 * @param[out] out_ra_slot_offset Receives the saved-RA slot offset.
 * @return `true` when the offsets are trustworthy for the current state;
 * `false` when FP-chain walking cannot be relied on (e.g. ARM in Thumb
 * state).
 */
template <typename AbiTraits>
[[nodiscard]] bool
resolve_frame_slot_offsets(register_context_ref reg_ctx,
                          ptrdiff_t &out_fp_slot_offset,
                          ptrdiff_t &out_ra_slot_offset) noexcept {
  if constexpr (detail::has_resolve_frame_slot_offsets<AbiTraits>::value) {
    return AbiTraits::resolve_frame_slot_offsets(reg_ctx, out_fp_slot_offset,
                                                out_ra_slot_offset);
  } else {
    out_fp_slot_offset = AbiTraits::fp_slot_offset;
    out_ra_slot_offset = AbiTraits::ra_slot_offset;
    return true;
  }
}

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