// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: MIT

#pragma once

/** @file register_view.hpp
 * @brief Formattable view for rendering CPU register contexts across all
 * architectures. */

#include "../microfmt.hpp"
#include "dwarf_abi.hpp"
#include "dwarf_registers.hpp"
#include "register_context.hpp"
#include <cstdint>

namespace microfmt {

/**
 * @brief Formattable view wrapping a register_context_ref for a specific
 * architecture ABI.
 * @tparam AbiTraits Architecture-specific ABI traits.
 */
template <typename AbiTraits> class register_context_view {
public:
  constexpr explicit register_context_view(
      register_context_ref reg_ctx) noexcept
      : reg_ctx_(reg_ctx) {}

  [[nodiscard]] constexpr register_context_ref reg_context() const noexcept {
    return reg_ctx_;
  }

private:
  register_context_ref reg_ctx_;
};

// ============================================================================
// x86_64 (AMD64) Formatter
// ============================================================================
template <> struct formatter<register_context_view<x86_64_abi_traits>> {
  void format(const register_context_view<x86_64_abi_traits> &view,
              const sink &out) const noexcept {
    register_context_ref reg_ctx = view.reg_context();
    if (!reg_ctx) {
      out.write("<null register context>");
      return;
    }

    using namespace dwarf::x86_64;
    uint64_t val = 0;

    const struct {
      const char *name;
      uint32_t id;
    } regs[] = {{"RAX", RAX}, {"RCX", RCX}, {"RDX", RDX}, {"RBX", RBX},
                {"RSP", RSP}, {"RBP", RBP}, {"RSI", RSI}, {"RDI", RDI},
                {"R8 ", R8},  {"R9 ", R9},  {"R10", R10}, {"R11", R11},
                {"R12", R12}, {"R13", R13}, {"R14", R14}, {"R15", R15},
                {"RIP", RIP}};

    bool first = true;
    for (const auto &r : regs) {
      if (reg_ctx.read_raw(r.id, &val, sizeof(val))) {
        if (!first)
          out.write("  ");
        microfmt::format_to(out, MICROFMT_STRING("{}={:#018x}"), r.name, val);
        first = false;
      }
    }
  }
};

// ============================================================================
// x86 (IA-32) Formatter
// ============================================================================
template <> struct formatter<register_context_view<x86_abi_traits>> {
  void format(const register_context_view<x86_abi_traits> &view,
              const sink &out) const noexcept {
    register_context_ref reg_ctx = view.reg_context();
    if (!reg_ctx) {
      out.write("<null register context>");
      return;
    }

    using namespace dwarf::x86;
    uint32_t val = 0;

    const struct {
      const char *name;
      uint32_t id;
    } regs[] = {{"EAX", EAX}, {"ECX", ECX}, {"EDX", EDX},
                {"EBX", EBX}, {"ESP", ESP}, {"EBP", EBP},
                {"ESI", ESI}, {"EDI", EDI}, {"EIP", EIP}};

    bool first = true;
    for (const auto &r : regs) {
      if (reg_ctx.read_raw(r.id, &val, sizeof(val))) {
        if (!first)
          out.write("  ");
        microfmt::format_to(out, MICROFMT_STRING("{}={:#010x}"), r.name, val);
        first = false;
      }
    }
  }
};

// ============================================================================
// AArch64 (64-bit ARM) Formatter
// ============================================================================
template <> struct formatter<register_context_view<aarch64_abi_traits>> {
  void format(const register_context_view<aarch64_abi_traits> &view,
              const sink &out) const noexcept {
    register_context_ref reg_ctx = view.reg_context();
    if (!reg_ctx) {
      out.write("<null register context>");
      return;
    }

    using namespace dwarf::aarch64;
    uint64_t val = 0;

    for (int i = 0; i <= 30; ++i) {
      if (reg_ctx.read_raw(i, &val, sizeof(val))) {
        const char *alias = "";
        uint64_t display_val = val;

        if (i == 29) {
          alias = " (FP)";
        } else if (i == 30) {
          alias = " (LR)";
          // Normalize link register to strip PAC/MTE bits
          display_val = aarch64_abi_traits::normalize_pc(val);
        }

        microfmt::format_to(out, MICROFMT_STRING("X{:<2}{}= {:#018x}\n"), i,
                            alias, display_val);
      }
    }

    if (reg_ctx.read_raw(SP, &val, sizeof(val))) {
      microfmt::format_to(out, MICROFMT_STRING("SP   = {:#018x}\n"), val);
    }

    if (reg_ctx.read_raw(PC, &val, sizeof(val))) {
      // Normalize program counter to strip PAC/MTE bits
      uint64_t normalized_pc = aarch64_abi_traits::normalize_pc(val);
      microfmt::format_to(out, MICROFMT_STRING("PC   = {:#018x}\n"),
                          normalized_pc);
    }
  }
};

// ============================================================================
// ARM32 (32-bit ARM EABI) Formatter
// ============================================================================
template <> struct formatter<register_context_view<arm_abi_traits>> {
  void format(const register_context_view<arm_abi_traits> &view,
              const sink &out) const noexcept {
    register_context_ref reg_ctx = view.reg_context();
    if (!reg_ctx) {
      out.write("<null register context>");
      return;
    }

    using namespace dwarf::arm32;
    uint32_t val = 0;

    const struct {
      const char *name;
      uint32_t id;
    } regs[] = {{"R0", R0},  {"R1", R1}, {"R2", R2},   {"R3", R3},
                {"R4", R4},  {"R5", R5}, {"R6", R6},   {"R7", R7},
                {"R8", R8},  {"R9", R9}, {"R10", R10}, {"FP", FP},
                {"IP", R12}, {"SP", SP}, {"LR", LR},   {"PC", PC}};

    bool first = true;
    for (const auto &r : regs) {
      if (reg_ctx.read_raw(r.id, &val, sizeof(val))) {
        if (!first)
          out.write("  ");
        microfmt::format_to(out, MICROFMT_STRING("{}={:#010x}"), r.name, val);
        first = false;
      }
    }
  }
};

// ============================================================================
// RISC-V 32-bit Formatter
// ============================================================================
template <> struct formatter<register_context_view<riscv32_abi_traits>> {
  void format(const register_context_view<riscv32_abi_traits> &view,
              const sink &out) const noexcept {
    register_context_ref reg_ctx = view.reg_context();
    if (!reg_ctx) {
      out.write("<null register context>");
      return;
    }

    using namespace dwarf::riscv;
    uint32_t val = 0;

    const struct {
      const char *name;
      uint32_t id;
    } regs[] = {{"zero", ZERO}, {"ra", RA}, {"sp", SP}, {"gp", GP}, {"tp", TP},
                {"t0", T0},     {"t1", T1}, {"t2", T2}, {"s0", S0}, {"s1", S1},
                {"a0", A0},     {"a1", A1}, {"a2", A2}, {"a3", A3}, {"a4", A4},
                {"a5", A5},     {"pc", PC}};

    bool first = true;
    for (const auto &r : regs) {
      if (reg_ctx.read_raw(r.id, &val, sizeof(val))) {
        if (!first)
          out.write("  ");
        microfmt::format_to(out, MICROFMT_STRING("{}={:#010x}"), r.name, val);
        first = false;
      }
    }
  }
};

// ============================================================================
// RISC-V 64-bit Formatter
// ============================================================================
template <> struct formatter<register_context_view<riscv64_abi_traits>> {
  void format(const register_context_view<riscv64_abi_traits> &view,
              const sink &out) const noexcept {
    register_context_ref reg_ctx = view.reg_context();
    if (!reg_ctx) {
      out.write("<null register context>");
      return;
    }

    using namespace dwarf::riscv;
    uint64_t val = 0;

    const struct {
      const char *name;
      uint32_t id;
    } regs[] = {{"zero", ZERO}, {"ra", RA}, {"sp", SP}, {"gp", GP}, {"tp", TP},
                {"t0", T0},     {"t1", T1}, {"t2", T2}, {"s0", S0}, {"s1", S1},
                {"a0", A0},     {"a1", A1}, {"a2", A2}, {"a3", A3}, {"a4", A4},
                {"a5", A5},     {"pc", PC}};

    bool first = true;
    for (const auto &r : regs) {
      if (reg_ctx.read_raw(r.id, &val, sizeof(val))) {
        if (!first)
          out.write("  ");
        microfmt::format_to(out, MICROFMT_STRING("{}={:#018x}"), r.name, val);
        first = false;
      }
    }
  }
};

} // namespace microfmt