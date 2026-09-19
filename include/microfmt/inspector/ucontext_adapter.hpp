// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file ucontext_adapter.hpp @brief POSIX ucontext_t / mcontext_t register context adapter for Linux and FreeBSD. */

#pragma once

#if !defined(__linux__) && !defined(__FreeBSD__)
#error "microfmt/inspector/ucontext_adapter.hpp only supports Linux and FreeBSD"
#endif

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <ucontext.h>

#include "../microfmt.hpp"
#include "dwarf_registers.hpp"
#include "register_context.hpp"

namespace microfmt::detail {

/**
 * @brief Reads a general-purpose register out of a POSIX `ucontext_t` by its
 * DWARF register index (see @ref microfmt::dwarf).
 *
 * Supports Linux and FreeBSD across x86, x86_64, ARM, AArch64, and RISC-V.
 * @p out_value may be any size from 1 up to `sizeof(uint64_t)` bytes: the
 * full register value is always read into a 64-bit temporary first, then
 * only the low @p value_size bytes are copied out, so 32-bit-register
 * architectures (x86, ARM32, RISC-V32) can be read with a native-width
 * `uint32_t` (or narrower) destination without truncation surprises on
 * little-endian targets. Unsupported register indices, architectures/OSes,
 * or an oversized @p value_size return `false` without touching
 * @p out_value.
 */
inline bool ucontext_read_register(const void *ctx, address_space_ref, uint32_t dwarf_reg_index, void *out_value,
                                   size_t value_size) noexcept {
  if (!ctx || !out_value || value_size == 0 || value_size > sizeof(uint64_t))
    return false;

  const auto &uc = *static_cast<const ucontext_t *>(ctx);
  uint64_t value = 0;
  bool found = false;

#if defined(__linux__)

#if defined(__x86_64__)
  // DWARF x86_64 register numbers do not match glibc's REG_* gregs[] index
  // order, so each register is mapped explicitly (System V ABI, see
  // dwarf_registers.hpp).
  switch (dwarf_reg_index) {
  case dwarf::x86_64::rax:
    value = static_cast<uint64_t>(uc.uc_mcontext.gregs[REG_RAX]);
    found = true;
    break;
  case dwarf::x86_64::rdx:
    value = static_cast<uint64_t>(uc.uc_mcontext.gregs[REG_RDX]);
    found = true;
    break;
  case dwarf::x86_64::rcx:
    value = static_cast<uint64_t>(uc.uc_mcontext.gregs[REG_RCX]);
    found = true;
    break;
  case dwarf::x86_64::rbx:
    value = static_cast<uint64_t>(uc.uc_mcontext.gregs[REG_RBX]);
    found = true;
    break;
  case dwarf::x86_64::rsi:
    value = static_cast<uint64_t>(uc.uc_mcontext.gregs[REG_RSI]);
    found = true;
    break;
  case dwarf::x86_64::rdi:
    value = static_cast<uint64_t>(uc.uc_mcontext.gregs[REG_RDI]);
    found = true;
    break;
  case dwarf::x86_64::rbp:
    value = static_cast<uint64_t>(uc.uc_mcontext.gregs[REG_RBP]);
    found = true;
    break;
  case dwarf::x86_64::rsp:
    value = static_cast<uint64_t>(uc.uc_mcontext.gregs[REG_RSP]);
    found = true;
    break;
  case dwarf::x86_64::r8:
    value = static_cast<uint64_t>(uc.uc_mcontext.gregs[REG_R8]);
    found = true;
    break;
  case dwarf::x86_64::r9:
    value = static_cast<uint64_t>(uc.uc_mcontext.gregs[REG_R9]);
    found = true;
    break;
  case dwarf::x86_64::r10:
    value = static_cast<uint64_t>(uc.uc_mcontext.gregs[REG_R10]);
    found = true;
    break;
  case dwarf::x86_64::r11:
    value = static_cast<uint64_t>(uc.uc_mcontext.gregs[REG_R11]);
    found = true;
    break;
  case dwarf::x86_64::r12:
    value = static_cast<uint64_t>(uc.uc_mcontext.gregs[REG_R12]);
    found = true;
    break;
  case dwarf::x86_64::r13:
    value = static_cast<uint64_t>(uc.uc_mcontext.gregs[REG_R13]);
    found = true;
    break;
  case dwarf::x86_64::r14:
    value = static_cast<uint64_t>(uc.uc_mcontext.gregs[REG_R14]);
    found = true;
    break;
  case dwarf::x86_64::r15:
    value = static_cast<uint64_t>(uc.uc_mcontext.gregs[REG_R15]);
    found = true;
    break;
  case dwarf::x86_64::rip:
    value = static_cast<uint64_t>(uc.uc_mcontext.gregs[REG_RIP]);
    found = true;
    break;
  default:
    break;
  }

#elif defined(__i386__)
  // DWARF x86 register numbers likewise do not match glibc's REG_* order.
  switch (dwarf_reg_index) {
  case dwarf::x86::eax:
    value = static_cast<uint64_t>(static_cast<uint32_t>(uc.uc_mcontext.gregs[REG_EAX]));
    found = true;
    break;
  case dwarf::x86::ecx:
    value = static_cast<uint64_t>(static_cast<uint32_t>(uc.uc_mcontext.gregs[REG_ECX]));
    found = true;
    break;
  case dwarf::x86::edx:
    value = static_cast<uint64_t>(static_cast<uint32_t>(uc.uc_mcontext.gregs[REG_EDX]));
    found = true;
    break;
  case dwarf::x86::ebx:
    value = static_cast<uint64_t>(static_cast<uint32_t>(uc.uc_mcontext.gregs[REG_EBX]));
    found = true;
    break;
  case dwarf::x86::esp:
    value = static_cast<uint64_t>(static_cast<uint32_t>(uc.uc_mcontext.gregs[REG_ESP]));
    found = true;
    break;
  case dwarf::x86::ebp:
    value = static_cast<uint64_t>(static_cast<uint32_t>(uc.uc_mcontext.gregs[REG_EBP]));
    found = true;
    break;
  case dwarf::x86::esi:
    value = static_cast<uint64_t>(static_cast<uint32_t>(uc.uc_mcontext.gregs[REG_ESI]));
    found = true;
    break;
  case dwarf::x86::edi:
    value = static_cast<uint64_t>(static_cast<uint32_t>(uc.uc_mcontext.gregs[REG_EDI]));
    found = true;
    break;
  case dwarf::x86::eip:
    value = static_cast<uint64_t>(static_cast<uint32_t>(uc.uc_mcontext.gregs[REG_EIP]));
    found = true;
    break;
  default:
    break;
  }

#elif defined(__aarch64__)
  // uc_mcontext.regs[0..30] holds x0-x30 directly; sp/pc are named fields.
  // This layout coincides with the DWARF AArch64 register numbering.
  if (dwarf_reg_index <= 30) {
    value = uc.uc_mcontext.regs[dwarf_reg_index];
    found = true;
  } else if (dwarf_reg_index == dwarf::aarch64::sp) {
    value = uc.uc_mcontext.sp;
    found = true;
  } else if (dwarf_reg_index == dwarf::aarch64::pc) {
    value = uc.uc_mcontext.pc;
    found = true;
  }

#elif defined(__arm__)
  // arm_r0..arm_r10, arm_fp, arm_ip, arm_sp, arm_lr, arm_pc map 1:1 onto
  // DWARF ARM32 r0-r15; arm_cpsr maps to dwarf::arm32::cpsr, needed to
  // detect the live ARM/Thumb instruction-set state (CPSR.T) when resolving
  // which register (R11 vs. R7) actually holds the frame pointer.
  switch (dwarf_reg_index) {
  case dwarf::arm32::r0:
    value = uc.uc_mcontext.arm_r0;
    found = true;
    break;
  case dwarf::arm32::r1:
    value = uc.uc_mcontext.arm_r1;
    found = true;
    break;
  case dwarf::arm32::r2:
    value = uc.uc_mcontext.arm_r2;
    found = true;
    break;
  case dwarf::arm32::r3:
    value = uc.uc_mcontext.arm_r3;
    found = true;
    break;
  case dwarf::arm32::r4:
    value = uc.uc_mcontext.arm_r4;
    found = true;
    break;
  case dwarf::arm32::r5:
    value = uc.uc_mcontext.arm_r5;
    found = true;
    break;
  case dwarf::arm32::r6:
    value = uc.uc_mcontext.arm_r6;
    found = true;
    break;
  case dwarf::arm32::r7:
    value = uc.uc_mcontext.arm_r7;
    found = true;
    break;
  case dwarf::arm32::r8:
    value = uc.uc_mcontext.arm_r8;
    found = true;
    break;
  case dwarf::arm32::r9:
    value = uc.uc_mcontext.arm_r9;
    found = true;
    break;
  case dwarf::arm32::r10:
    value = uc.uc_mcontext.arm_r10;
    found = true;
    break;
  case dwarf::arm32::r11:
    value = uc.uc_mcontext.arm_fp;
    found = true;
    break;
  case dwarf::arm32::r12:
    value = uc.uc_mcontext.arm_ip;
    found = true;
    break;
  case dwarf::arm32::r13:
    value = uc.uc_mcontext.arm_sp;
    found = true;
    break;
  case dwarf::arm32::r14:
    value = uc.uc_mcontext.arm_lr;
    found = true;
    break;
  case dwarf::arm32::r15:
    value = uc.uc_mcontext.arm_pc;
    found = true;
    break;
  case dwarf::arm32::cpsr:
    value = uc.uc_mcontext.arm_cpsr;
    found = true;
    break;
  default:
    break;
  }

#elif defined(__riscv) && (__riscv_xlen == 64)
  // glibc's mcontext_t.__gregs[0] holds pc; __gregs[1..31] hold x1-x31.
  // x0 ("zero") is architecturally hardwired to zero and is never saved.
  if (dwarf_reg_index == dwarf::riscv::zero) {
    value = 0;
    found = true;
  } else if (dwarf_reg_index >= 1 && dwarf_reg_index <= 31) {
    value = static_cast<uint64_t>(uc.uc_mcontext.__gregs[dwarf_reg_index]);
    found = true;
  } else if (dwarf_reg_index == dwarf::riscv::pc) {
    value = static_cast<uint64_t>(uc.uc_mcontext.__gregs[0]);
    found = true;
  }
#endif

#elif defined(__FreeBSD__)

#if defined(__amd64__) || defined(__x86_64__)
  switch (dwarf_reg_index) {
  case dwarf::x86_64::rax:
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_rax);
    found = true;
    break;
  case dwarf::x86_64::rdx:
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_rdx);
    found = true;
    break;
  case dwarf::x86_64::rcx:
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_rcx);
    found = true;
    break;
  case dwarf::x86_64::rbx:
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_rbx);
    found = true;
    break;
  case dwarf::x86_64::rsi:
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_rsi);
    found = true;
    break;
  case dwarf::x86_64::rdi:
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_rdi);
    found = true;
    break;
  case dwarf::x86_64::rbp:
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_rbp);
    found = true;
    break;
  case dwarf::x86_64::rsp:
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_rsp);
    found = true;
    break;
  case dwarf::x86_64::r8:
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_r8);
    found = true;
    break;
  case dwarf::x86_64::r9:
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_r9);
    found = true;
    break;
  case dwarf::x86_64::r10:
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_r10);
    found = true;
    break;
  case dwarf::x86_64::r11:
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_r11);
    found = true;
    break;
  case dwarf::x86_64::r12:
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_r12);
    found = true;
    break;
  case dwarf::x86_64::r13:
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_r13);
    found = true;
    break;
  case dwarf::x86_64::r14:
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_r14);
    found = true;
    break;
  case dwarf::x86_64::r15:
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_r15);
    found = true;
    break;
  case dwarf::x86_64::rip:
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_rip);
    found = true;
    break;
  default:
    break;
  }

#elif defined(__i386__)
  switch (dwarf_reg_index) {
  case dwarf::x86::eax:
    value = static_cast<uint64_t>(static_cast<uint32_t>(uc.uc_mcontext.mc_eax));
    found = true;
    break;
  case dwarf::x86::ecx:
    value = static_cast<uint64_t>(static_cast<uint32_t>(uc.uc_mcontext.mc_ecx));
    found = true;
    break;
  case dwarf::x86::edx:
    value = static_cast<uint64_t>(static_cast<uint32_t>(uc.uc_mcontext.mc_edx));
    found = true;
    break;
  case dwarf::x86::ebx:
    value = static_cast<uint64_t>(static_cast<uint32_t>(uc.uc_mcontext.mc_ebx));
    found = true;
    break;
  case dwarf::x86::esp:
    value = static_cast<uint64_t>(static_cast<uint32_t>(uc.uc_mcontext.mc_esp));
    found = true;
    break;
  case dwarf::x86::ebp:
    value = static_cast<uint64_t>(static_cast<uint32_t>(uc.uc_mcontext.mc_ebp));
    found = true;
    break;
  case dwarf::x86::esi:
    value = static_cast<uint64_t>(static_cast<uint32_t>(uc.uc_mcontext.mc_esi));
    found = true;
    break;
  case dwarf::x86::edi:
    value = static_cast<uint64_t>(static_cast<uint32_t>(uc.uc_mcontext.mc_edi));
    found = true;
    break;
  case dwarf::x86::eip:
    value = static_cast<uint64_t>(static_cast<uint32_t>(uc.uc_mcontext.mc_eip));
    found = true;
    break;
  default:
    break;
  }

#elif defined(__aarch64__)
  // struct gpregs { register_t gp_x[30]; gp_lr; gp_sp; gp_elr; gp_spsr; };
  // x30 (lr) is a dedicated field, NOT gp_x[30] (which would be OOB: gp_x
  // only holds x0-x29).
  if (dwarf_reg_index <= 29) {
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_gpregs.gp_x[dwarf_reg_index]);
    found = true;
  } else if (dwarf_reg_index == 30) {
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_gpregs.gp_lr);
    found = true;
  } else if (dwarf_reg_index == dwarf::aarch64::sp) {
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_gpregs.gp_sp);
    found = true;
  } else if (dwarf_reg_index == dwarf::aarch64::pc) {
    value = static_cast<uint64_t>(uc.uc_mcontext.mc_gpregs.gp_elr);
    found = true;
  }

#elif defined(__arm__)
  // struct mcontext_t { __gregset_t __gregs[17]; ... }; _REG_R0.._REG_R15
  // are 0..15, matching DWARF ARM32 r0-r15 directly; _REG_CPSR is 16 (no
  // DWARF equivalent handled here).
  if (dwarf_reg_index <= 15) {
    value = static_cast<uint64_t>(uc.uc_mcontext.__gregs[dwarf_reg_index]);
    found = true;
  }

#elif defined(__riscv) && (__riscv_xlen == 64)
  // struct gpregs { gp_ra; gp_sp; gp_gp; gp_tp; gp_t[7]; gp_s[12]; gp_a[8];
  // gp_sepc; gp_sstatus; }; x0 ("zero") is hardwired and never saved.
  const auto &gp = uc.uc_mcontext.mc_gpregs;
  if (dwarf_reg_index == dwarf::riscv::zero) {
    value = 0;
    found = true;
  } else if (dwarf_reg_index == dwarf::riscv::ra) {
    value = static_cast<uint64_t>(gp.gp_ra);
    found = true;
  } else if (dwarf_reg_index == dwarf::riscv::sp) {
    value = static_cast<uint64_t>(gp.gp_sp);
    found = true;
  } else if (dwarf_reg_index == dwarf::riscv::gp) {
    value = static_cast<uint64_t>(gp.gp_gp);
    found = true;
  } else if (dwarf_reg_index == dwarf::riscv::tp) {
    value = static_cast<uint64_t>(gp.gp_tp);
    found = true;
  } else if (dwarf_reg_index >= dwarf::riscv::t0 && dwarf_reg_index <= dwarf::riscv::t2) {
    value = static_cast<uint64_t>(gp.gp_t[dwarf_reg_index - dwarf::riscv::t0]);
    found = true;
  } else if (dwarf_reg_index >= dwarf::riscv::s0 && dwarf_reg_index <= dwarf::riscv::s1) {
    value = static_cast<uint64_t>(gp.gp_s[dwarf_reg_index - dwarf::riscv::s0]);
    found = true;
  } else if (dwarf_reg_index >= dwarf::riscv::a0 && dwarf_reg_index <= dwarf::riscv::a7) {
    value = static_cast<uint64_t>(gp.gp_a[dwarf_reg_index - dwarf::riscv::a0]);
    found = true;
  } else if (dwarf_reg_index >= dwarf::riscv::s2 && dwarf_reg_index <= dwarf::riscv::s11) {
    value = static_cast<uint64_t>(gp.gp_s[2 + (dwarf_reg_index - dwarf::riscv::s2)]);
    found = true;
  } else if (dwarf_reg_index >= dwarf::riscv::t3 && dwarf_reg_index <= dwarf::riscv::t6) {
    value = static_cast<uint64_t>(gp.gp_t[3 + (dwarf_reg_index - dwarf::riscv::t3)]);
    found = true;
  } else if (dwarf_reg_index == dwarf::riscv::pc) {
    value = static_cast<uint64_t>(gp.gp_sepc);
    found = true;
  }
#endif

#endif

  if (!found)
    return false;
  std::memcpy(out_value, &value, value_size);
  return true;
}

/**
 * @brief Writes a general-purpose register into a POSIX `ucontext_t` by its
 * DWARF register index (see @ref microfmt::dwarf).
 *
 * Mirrors @ref ucontext_read_register; supports the same architectures and
 * OSes. @p in_value may be any size from 1 up to `sizeof(uint64_t)` bytes:
 * the bytes are copied into the low bits of a zero-initialized 64-bit
 * temporary before truncating to the target register's native width, so
 * 32-bit-register architectures can be written from a native-width
 * `uint32_t` (or narrower) source. Unsupported register indices,
 * architectures/OSes, or an oversized @p value_size return `false` without
 * modifying @p ctx.
 */
inline bool ucontext_write_register(void *ctx, address_space_ref, uint32_t dwarf_reg_index, const void *in_value,
                                    size_t value_size) noexcept {
  if (!ctx || !in_value || value_size == 0 || value_size > sizeof(uint64_t))
    return false;

  uint64_t value = 0;
  std::memcpy(&value, in_value, value_size);
  auto &uc = *static_cast<ucontext_t *>(ctx);
  bool found = false;

#if defined(__linux__)

#if defined(__x86_64__)
  switch (dwarf_reg_index) {
  case dwarf::x86_64::rax:
    uc.uc_mcontext.gregs[REG_RAX] = static_cast<greg_t>(value);
    found = true;
    break;
  case dwarf::x86_64::rdx:
    uc.uc_mcontext.gregs[REG_RDX] = static_cast<greg_t>(value);
    found = true;
    break;
  case dwarf::x86_64::rcx:
    uc.uc_mcontext.gregs[REG_RCX] = static_cast<greg_t>(value);
    found = true;
    break;
  case dwarf::x86_64::rbx:
    uc.uc_mcontext.gregs[REG_RBX] = static_cast<greg_t>(value);
    found = true;
    break;
  case dwarf::x86_64::rsi:
    uc.uc_mcontext.gregs[REG_RSI] = static_cast<greg_t>(value);
    found = true;
    break;
  case dwarf::x86_64::rdi:
    uc.uc_mcontext.gregs[REG_RDI] = static_cast<greg_t>(value);
    found = true;
    break;
  case dwarf::x86_64::rbp:
    uc.uc_mcontext.gregs[REG_RBP] = static_cast<greg_t>(value);
    found = true;
    break;
  case dwarf::x86_64::rsp:
    uc.uc_mcontext.gregs[REG_RSP] = static_cast<greg_t>(value);
    found = true;
    break;
  case dwarf::x86_64::r8:
    uc.uc_mcontext.gregs[REG_R8] = static_cast<greg_t>(value);
    found = true;
    break;
  case dwarf::x86_64::r9:
    uc.uc_mcontext.gregs[REG_R9] = static_cast<greg_t>(value);
    found = true;
    break;
  case dwarf::x86_64::r10:
    uc.uc_mcontext.gregs[REG_R10] = static_cast<greg_t>(value);
    found = true;
    break;
  case dwarf::x86_64::r11:
    uc.uc_mcontext.gregs[REG_R11] = static_cast<greg_t>(value);
    found = true;
    break;
  case dwarf::x86_64::r12:
    uc.uc_mcontext.gregs[REG_R12] = static_cast<greg_t>(value);
    found = true;
    break;
  case dwarf::x86_64::r13:
    uc.uc_mcontext.gregs[REG_R13] = static_cast<greg_t>(value);
    found = true;
    break;
  case dwarf::x86_64::r14:
    uc.uc_mcontext.gregs[REG_R14] = static_cast<greg_t>(value);
    found = true;
    break;
  case dwarf::x86_64::r15:
    uc.uc_mcontext.gregs[REG_R15] = static_cast<greg_t>(value);
    found = true;
    break;
  case dwarf::x86_64::rip:
    uc.uc_mcontext.gregs[REG_RIP] = static_cast<greg_t>(value);
    found = true;
    break;
  default:
    break;
  }

#elif defined(__i386__)
  switch (dwarf_reg_index) {
  case dwarf::x86::eax:
    uc.uc_mcontext.gregs[REG_EAX] = static_cast<greg_t>(static_cast<uint32_t>(value));
    found = true;
    break;
  case dwarf::x86::ecx:
    uc.uc_mcontext.gregs[REG_ECX] = static_cast<greg_t>(static_cast<uint32_t>(value));
    found = true;
    break;
  case dwarf::x86::edx:
    uc.uc_mcontext.gregs[REG_EDX] = static_cast<greg_t>(static_cast<uint32_t>(value));
    found = true;
    break;
  case dwarf::x86::ebx:
    uc.uc_mcontext.gregs[REG_EBX] = static_cast<greg_t>(static_cast<uint32_t>(value));
    found = true;
    break;
  case dwarf::x86::esp:
    uc.uc_mcontext.gregs[REG_ESP] = static_cast<greg_t>(static_cast<uint32_t>(value));
    found = true;
    break;
  case dwarf::x86::ebp:
    uc.uc_mcontext.gregs[REG_EBP] = static_cast<greg_t>(static_cast<uint32_t>(value));
    found = true;
    break;
  case dwarf::x86::esi:
    uc.uc_mcontext.gregs[REG_ESI] = static_cast<greg_t>(static_cast<uint32_t>(value));
    found = true;
    break;
  case dwarf::x86::edi:
    uc.uc_mcontext.gregs[REG_EDI] = static_cast<greg_t>(static_cast<uint32_t>(value));
    found = true;
    break;
  case dwarf::x86::eip:
    uc.uc_mcontext.gregs[REG_EIP] = static_cast<greg_t>(static_cast<uint32_t>(value));
    found = true;
    break;
  default:
    break;
  }

#elif defined(__aarch64__)
  if (dwarf_reg_index <= 30) {
    uc.uc_mcontext.regs[dwarf_reg_index] = value;
    found = true;
  } else if (dwarf_reg_index == dwarf::aarch64::sp) {
    uc.uc_mcontext.sp = value;
    found = true;
  } else if (dwarf_reg_index == dwarf::aarch64::pc) {
    uc.uc_mcontext.pc = value;
    found = true;
  }

#elif defined(__arm__)
  switch (dwarf_reg_index) {
  case dwarf::arm32::r0:
    uc.uc_mcontext.arm_r0 = static_cast<uint32_t>(value);
    found = true;
    break;
  case dwarf::arm32::r1:
    uc.uc_mcontext.arm_r1 = static_cast<uint32_t>(value);
    found = true;
    break;
  case dwarf::arm32::r2:
    uc.uc_mcontext.arm_r2 = static_cast<uint32_t>(value);
    found = true;
    break;
  case dwarf::arm32::r3:
    uc.uc_mcontext.arm_r3 = static_cast<uint32_t>(value);
    found = true;
    break;
  case dwarf::arm32::r4:
    uc.uc_mcontext.arm_r4 = static_cast<uint32_t>(value);
    found = true;
    break;
  case dwarf::arm32::r5:
    uc.uc_mcontext.arm_r5 = static_cast<uint32_t>(value);
    found = true;
    break;
  case dwarf::arm32::r6:
    uc.uc_mcontext.arm_r6 = static_cast<uint32_t>(value);
    found = true;
    break;
  case dwarf::arm32::r7:
    uc.uc_mcontext.arm_r7 = static_cast<uint32_t>(value);
    found = true;
    break;
  case dwarf::arm32::r8:
    uc.uc_mcontext.arm_r8 = static_cast<uint32_t>(value);
    found = true;
    break;
  case dwarf::arm32::r9:
    uc.uc_mcontext.arm_r9 = static_cast<uint32_t>(value);
    found = true;
    break;
  case dwarf::arm32::r10:
    uc.uc_mcontext.arm_r10 = static_cast<uint32_t>(value);
    found = true;
    break;
  case dwarf::arm32::r11:
    uc.uc_mcontext.arm_fp = static_cast<uint32_t>(value);
    found = true;
    break;
  case dwarf::arm32::r12:
    uc.uc_mcontext.arm_ip = static_cast<uint32_t>(value);
    found = true;
    break;
  case dwarf::arm32::r13:
    uc.uc_mcontext.arm_sp = static_cast<uint32_t>(value);
    found = true;
    break;
  case dwarf::arm32::r14:
    uc.uc_mcontext.arm_lr = static_cast<uint32_t>(value);
    found = true;
    break;
  case dwarf::arm32::r15:
    uc.uc_mcontext.arm_pc = static_cast<uint32_t>(value);
    found = true;
    break;
  case dwarf::arm32::cpsr:
    uc.uc_mcontext.arm_cpsr = static_cast<uint32_t>(value);
    found = true;
    break;
  default:
    break;
  }

#elif defined(__riscv) && (__riscv_xlen == 64)
  // x0 ("zero") is hardwired and cannot be written; silently accept the
  // request as a no-op success, matching hardware semantics.
  if (dwarf_reg_index == dwarf::riscv::zero) {
    found = true;
  } else if (dwarf_reg_index >= 1 && dwarf_reg_index <= 31) {
    uc.uc_mcontext.__gregs[dwarf_reg_index] = static_cast<unsigned long>(value);
    found = true;
  } else if (dwarf_reg_index == dwarf::riscv::pc) {
    uc.uc_mcontext.__gregs[0] = static_cast<unsigned long>(value);
    found = true;
  }
#endif

#elif defined(__FreeBSD__)

#if defined(__amd64__) || defined(__x86_64__)
  switch (dwarf_reg_index) {
  case dwarf::x86_64::rax:
    uc.uc_mcontext.mc_rax = static_cast<decltype(uc.uc_mcontext.mc_rax)>(value);
    found = true;
    break;
  case dwarf::x86_64::rdx:
    uc.uc_mcontext.mc_rdx = static_cast<decltype(uc.uc_mcontext.mc_rdx)>(value);
    found = true;
    break;
  case dwarf::x86_64::rcx:
    uc.uc_mcontext.mc_rcx = static_cast<decltype(uc.uc_mcontext.mc_rcx)>(value);
    found = true;
    break;
  case dwarf::x86_64::rbx:
    uc.uc_mcontext.mc_rbx = static_cast<decltype(uc.uc_mcontext.mc_rbx)>(value);
    found = true;
    break;
  case dwarf::x86_64::rsi:
    uc.uc_mcontext.mc_rsi = static_cast<decltype(uc.uc_mcontext.mc_rsi)>(value);
    found = true;
    break;
  case dwarf::x86_64::rdi:
    uc.uc_mcontext.mc_rdi = static_cast<decltype(uc.uc_mcontext.mc_rdi)>(value);
    found = true;
    break;
  case dwarf::x86_64::rbp:
    uc.uc_mcontext.mc_rbp = static_cast<decltype(uc.uc_mcontext.mc_rbp)>(value);
    found = true;
    break;
  case dwarf::x86_64::rsp:
    uc.uc_mcontext.mc_rsp = static_cast<decltype(uc.uc_mcontext.mc_rsp)>(value);
    found = true;
    break;
  case dwarf::x86_64::r8:
    uc.uc_mcontext.mc_r8 = static_cast<decltype(uc.uc_mcontext.mc_r8)>(value);
    found = true;
    break;
  case dwarf::x86_64::r9:
    uc.uc_mcontext.mc_r9 = static_cast<decltype(uc.uc_mcontext.mc_r9)>(value);
    found = true;
    break;
  case dwarf::x86_64::r10:
    uc.uc_mcontext.mc_r10 = static_cast<decltype(uc.uc_mcontext.mc_r10)>(value);
    found = true;
    break;
  case dwarf::x86_64::r11:
    uc.uc_mcontext.mc_r11 = static_cast<decltype(uc.uc_mcontext.mc_r11)>(value);
    found = true;
    break;
  case dwarf::x86_64::r12:
    uc.uc_mcontext.mc_r12 = static_cast<decltype(uc.uc_mcontext.mc_r12)>(value);
    found = true;
    break;
  case dwarf::x86_64::r13:
    uc.uc_mcontext.mc_r13 = static_cast<decltype(uc.uc_mcontext.mc_r13)>(value);
    found = true;
    break;
  case dwarf::x86_64::r14:
    uc.uc_mcontext.mc_r14 = static_cast<decltype(uc.uc_mcontext.mc_r14)>(value);
    found = true;
    break;
  case dwarf::x86_64::r15:
    uc.uc_mcontext.mc_r15 = static_cast<decltype(uc.uc_mcontext.mc_r15)>(value);
    found = true;
    break;
  case dwarf::x86_64::rip:
    uc.uc_mcontext.mc_rip = static_cast<decltype(uc.uc_mcontext.mc_rip)>(value);
    found = true;
    break;
  default:
    break;
  }

#elif defined(__i386__)
  switch (dwarf_reg_index) {
  case dwarf::x86::eax:
    uc.uc_mcontext.mc_eax = static_cast<decltype(uc.uc_mcontext.mc_eax)>(static_cast<uint32_t>(value));
    found = true;
    break;
  case dwarf::x86::ecx:
    uc.uc_mcontext.mc_ecx = static_cast<decltype(uc.uc_mcontext.mc_ecx)>(static_cast<uint32_t>(value));
    found = true;
    break;
  case dwarf::x86::edx:
    uc.uc_mcontext.mc_edx = static_cast<decltype(uc.uc_mcontext.mc_edx)>(static_cast<uint32_t>(value));
    found = true;
    break;
  case dwarf::x86::ebx:
    uc.uc_mcontext.mc_ebx = static_cast<decltype(uc.uc_mcontext.mc_ebx)>(static_cast<uint32_t>(value));
    found = true;
    break;
  case dwarf::x86::esp:
    uc.uc_mcontext.mc_esp = static_cast<decltype(uc.uc_mcontext.mc_esp)>(static_cast<uint32_t>(value));
    found = true;
    break;
  case dwarf::x86::ebp:
    uc.uc_mcontext.mc_ebp = static_cast<decltype(uc.uc_mcontext.mc_ebp)>(static_cast<uint32_t>(value));
    found = true;
    break;
  case dwarf::x86::esi:
    uc.uc_mcontext.mc_esi = static_cast<decltype(uc.uc_mcontext.mc_esi)>(static_cast<uint32_t>(value));
    found = true;
    break;
  case dwarf::x86::edi:
    uc.uc_mcontext.mc_edi = static_cast<decltype(uc.uc_mcontext.mc_edi)>(static_cast<uint32_t>(value));
    found = true;
    break;
  case dwarf::x86::eip:
    uc.uc_mcontext.mc_eip = static_cast<decltype(uc.uc_mcontext.mc_eip)>(static_cast<uint32_t>(value));
    found = true;
    break;
  default:
    break;
  }

#elif defined(__aarch64__)
  if (dwarf_reg_index <= 29) {
    uc.uc_mcontext.mc_gpregs.gp_x[dwarf_reg_index] =
        static_cast<std::remove_reference_t<decltype(uc.uc_mcontext.mc_gpregs.gp_x[0])>>(value);
    found = true;
  } else if (dwarf_reg_index == 30) {
    uc.uc_mcontext.mc_gpregs.gp_lr = static_cast<decltype(uc.uc_mcontext.mc_gpregs.gp_lr)>(value);
    found = true;
  } else if (dwarf_reg_index == dwarf::aarch64::sp) {
    uc.uc_mcontext.mc_gpregs.gp_sp = static_cast<decltype(uc.uc_mcontext.mc_gpregs.gp_sp)>(value);
    found = true;
  } else if (dwarf_reg_index == dwarf::aarch64::pc) {
    uc.uc_mcontext.mc_gpregs.gp_elr = static_cast<decltype(uc.uc_mcontext.mc_gpregs.gp_elr)>(value);
    found = true;
  }

#elif defined(__arm__)
  if (dwarf_reg_index <= 15) {
    uc.uc_mcontext.__gregs[dwarf_reg_index] = static_cast<__greg_t>(value);
    found = true;
  }

#elif defined(__riscv) && (__riscv_xlen == 64)
  auto &gp = uc.uc_mcontext.mc_gpregs;
  if (dwarf_reg_index == dwarf::riscv::zero) {
    // x0 ("zero") is hardwired and cannot be written; accept as a no-op.
    found = true;
  } else if (dwarf_reg_index == dwarf::riscv::ra) {
    gp.gp_ra = static_cast<decltype(gp.gp_ra)>(value);
    found = true;
  } else if (dwarf_reg_index == dwarf::riscv::sp) {
    gp.gp_sp = static_cast<decltype(gp.gp_sp)>(value);
    found = true;
  } else if (dwarf_reg_index == dwarf::riscv::gp) {
    gp.gp_gp = static_cast<decltype(gp.gp_gp)>(value);
    found = true;
  } else if (dwarf_reg_index == dwarf::riscv::tp) {
    gp.gp_tp = static_cast<decltype(gp.gp_tp)>(value);
    found = true;
  } else if (dwarf_reg_index >= dwarf::riscv::t0 && dwarf_reg_index <= dwarf::riscv::t2) {
    gp.gp_t[dwarf_reg_index - dwarf::riscv::t0] = static_cast<std::remove_reference_t<decltype(gp.gp_t[0])>>(value);
    found = true;
  } else if (dwarf_reg_index >= dwarf::riscv::s0 && dwarf_reg_index <= dwarf::riscv::s1) {
    gp.gp_s[dwarf_reg_index - dwarf::riscv::s0] = static_cast<std::remove_reference_t<decltype(gp.gp_s[0])>>(value);
    found = true;
  } else if (dwarf_reg_index >= dwarf::riscv::a0 && dwarf_reg_index <= dwarf::riscv::a7) {
    gp.gp_a[dwarf_reg_index - dwarf::riscv::a0] = static_cast<std::remove_reference_t<decltype(gp.gp_a[0])>>(value);
    found = true;
  } else if (dwarf_reg_index >= dwarf::riscv::s2 && dwarf_reg_index <= dwarf::riscv::s11) {
    gp.gp_s[2 + (dwarf_reg_index - dwarf::riscv::s2)] =
        static_cast<std::remove_reference_t<decltype(gp.gp_s[0])>>(value);
    found = true;
  } else if (dwarf_reg_index >= dwarf::riscv::t3 && dwarf_reg_index <= dwarf::riscv::t6) {
    gp.gp_t[3 + (dwarf_reg_index - dwarf::riscv::t3)] =
        static_cast<std::remove_reference_t<decltype(gp.gp_t[0])>>(value);
    found = true;
  } else if (dwarf_reg_index == dwarf::riscv::pc) {
    gp.gp_sepc = static_cast<decltype(gp.gp_sepc)>(value);
    found = true;
  }
#endif

#endif

  return found;
}

} // namespace microfmt::detail

namespace microfmt {

/**
 * @brief Creates a read-only, type-erased @ref register_context_ref backed
 * by a POSIX `ucontext_t` (e.g. a signal handler's third argument).
 *
 * Registers are addressed by DWARF index (see @ref microfmt::dwarf);
 * supports Linux and FreeBSD across x86, x86_64, ARM, AArch64, and RISC-V
 * with zero heap allocations or exceptions.
 *
 * @param uctx Context to read registers from; must outlive the returned ref.
 * @param space Address space used for any subsequent memory reads (e.g. by
 * an unwinder consuming this context); unrelated to register reads.
 * @param scratch Reusable scratch buffer forwarded to the returned ref.
 */
[[nodiscard]] inline register_context_ref
make_ucontext_register_context_ref(const ucontext_t &uctx MICROFMT_LIFETIMEBOUND, address_space_ref space,
                                   span<std::byte> scratch MICROFMT_LIFETIMEBOUND) noexcept {
  return make_read_only_register_context_ref<&detail::ucontext_read_register>(uctx, space, scratch);
}

/**
 * @brief Creates a mutable, type-erased @ref register_context_ref backed by
 * a POSIX `ucontext_t`, supporting both register reads and writes.
 *
 * Useful for adjusting a captured or signal-delivered context in place
 * (e.g. redirecting `pc`/`sp` before a `setcontext()`/`swapcontext()`
 * resume). Registers are addressed by DWARF index (see @ref microfmt::dwarf);
 * supports Linux and FreeBSD across x86, x86_64, ARM, AArch64, and RISC-V
 * with zero heap allocations or exceptions.
 *
 * @param uctx Context to read/write registers on; must outlive the returned
 * ref. Takes a non-const reference/pointer since writes mutate it in place.
 * @param space Address space used for any subsequent memory reads (e.g. by
 * an unwinder consuming this context); unrelated to register reads/writes.
 * @param scratch Reusable scratch buffer forwarded to the returned ref.
 */
[[nodiscard]] inline register_context_ref
make_mutable_ucontext_register_context_ref(ucontext_t &uctx MICROFMT_LIFETIMEBOUND, address_space_ref space,
                                           span<std::byte> scratch MICROFMT_LIFETIMEBOUND) noexcept {
  return make_register_context_ref<&detail::ucontext_read_register, &detail::ucontext_write_register>(uctx, space,
                                                                                                      scratch);
}

} // namespace microfmt
