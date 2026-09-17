// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "../array.hpp"
#include "../string_view.hpp"
#include <cstdint>

namespace microfmt::dwarf {

struct register_descriptor {
  microfmt::string_view name;
  uint32_t index;
};

namespace generic_timer {
enum : uint32_t {
  cntfrq = 320,
  cntpct = 321,
  cntvct = 322,
  cntp_tval = 323,
  cntp_ctl = 324,
  cntp_cval = 325,
  cntv_tval = 326,
  cntv_ctl = 327,
  cntv_cval = 328,
  cnthp_tval = 329,
  cnthp_ctl = 330,
  cnthp_cval = 331,
  cntvoff = 332,
  cnthctl = 333,
  cnthv_tval = 334,
  cnthv_ctl = 335,
  cnthv_cval = 336,
  cnthps_tval = 337,
  cnthps_ctl = 338,
  cnthps_cval = 339,
  cnthvs_tval = 340,
  cnthvs_ctl = 341,
  cnthvs_cval = 342,
  cntkctl = 343
};
} // namespace generic_timer

// ============================================================================
// x86_64 (AMD64) DWARF Register Numbers (System V ABI)
// ============================================================================
namespace x86_64 {
enum : uint32_t {
  rax = 0,
  rdx = 1,
  rcx = 2,
  rbx = 3,
  rsi = 4,
  rdi = 5,
  rbp = 6,
  rsp = 7,
  r8 = 8,
  r9 = 9,
  r10 = 10,
  r11 = 11,
  r12 = 12,
  r13 = 13,
  r14 = 14,
  r15 = 15,
  rip = 16,

  // Semantic Platform Aliases
  fp = rbp,
  sp = rsp,
  pc = rip,

  // Floating Point & SSE/AVX Vector Registers (XMM0 - XMM15)
  xmm0 = 17,
  xmm1 = 18,
  xmm2 = 19,
  xmm3 = 20,
  xmm4 = 21,
  xmm5 = 22,
  xmm6 = 23,
  xmm7 = 24,
  xmm8 = 25,
  xmm9 = 26,
  xmm10 = 27,
  xmm11 = 28,
  xmm12 = 29,
  xmm13 = 30,
  xmm14 = 31,
  xmm15 = 32,

  // Status & Control
  eflags = 49,

  // --- Segments & Base Registers ---
  fs_base = 58,
  gs_base = 59,
  cs = 60,
  ss = 61,
  ds = 62,
  es = 63,
  fs = 64,
  gs = 65,

  // --- Kernel / Privileged & Debug Registers (Extended IDs) ---
  kernel_gs_base = 66, // MSR IA32_KERNEL_GS_BASE (SwapGS target)
  cr0 = 100,
  cr2 = 102,
  cr3 = 103, // Page table base (PML4 / CR3)
  cr4 = 104,
  cr8 = 108, // Task Priority Register (TPR)
  dr0 = 120, // Hardware Breakpoints / Debug Registers
  dr1 = 121,
  dr2 = 122,
  dr3 = 123,
  dr6 = 126, // Debug Status
  dr7 = 127  // Debug Control
};

struct register_traits {
  inline static constexpr microfmt::array gpr_registers{
      register_descriptor{"RAX", rax}, register_descriptor{"RCX", rcx},
      register_descriptor{"RDX", rdx}, register_descriptor{"RBX", rbx},
      register_descriptor{"RSP", rsp}, register_descriptor{"RBP", rbp},
      register_descriptor{"RSI", rsi}, register_descriptor{"RDI", rdi},
      register_descriptor{"R8", r8},   register_descriptor{"R9", r9},
      register_descriptor{"R10", r10}, register_descriptor{"R11", r11},
      register_descriptor{"R12", r12}, register_descriptor{"R13", r13},
      register_descriptor{"R14", r14}, register_descriptor{"R15", r15},
      register_descriptor{"RIP", rip}};

  [[nodiscard]] static constexpr microfmt::array<register_descriptor, 15>
  address_registers() noexcept {
    return {{{"RBP", rbp}, {"RDI", rdi}, {"RSI", rsi}, {"RBX", rbx},
             {"R12", r12}, {"R13", r13}, {"R14", r14}, {"R15", r15},
             {"RAX", rax}, {"RCX", rcx}, {"RDX", rdx}, {"R8", r8},
             {"R9", r9}, {"R10", r10}, {"R11", r11}}};
  }

  inline static constexpr microfmt::array system_registers{
      register_descriptor{"EFLAGS", eflags},
      register_descriptor{"FS_BASE", fs_base},
      register_descriptor{"GS_BASE", gs_base},
      register_descriptor{"KERNEL_GS_BASE", kernel_gs_base},
      register_descriptor{"CS", cs},
      register_descriptor{"SS", ss},
      register_descriptor{"DS", ds},
      register_descriptor{"ES", es},
      register_descriptor{"FS", fs},
      register_descriptor{"GS", gs},
      register_descriptor{"CR0", cr0},
      register_descriptor{"CR2", cr2},
      register_descriptor{"CR3", cr3},
      register_descriptor{"CR4", cr4},
      register_descriptor{"CR8", cr8},
      register_descriptor{"DR0", dr0},
      register_descriptor{"DR1", dr1},
      register_descriptor{"DR2", dr2},
      register_descriptor{"DR3", dr3},
      register_descriptor{"DR6", dr6},
      register_descriptor{"DR7", dr7}};
};
} // namespace x86_64

// ============================================================================
// x86 (IA-32) DWARF Register Numbers
// ============================================================================
namespace x86 {
enum : uint32_t {
  eax = 0,
  ecx = 1,
  edx = 2,
  ebx = 3,
  esp = 4,
  ebp = 5,
  esi = 6,
  edi = 7,
  eip = 8,

  // Semantic Platform Aliases
  fp = ebp,
  sp = esp,
  pc = eip,

  // Floating Point Stack (ST0 - ST7)
  st0 = 11,
  st1 = 12,
  st2 = 13,
  st3 = 14,
  st4 = 15,
  st5 = 16,
  st6 = 17,
  st7 = 18,

  // SSE Vector Registers (XMM0 - XMM7)
  xmm0 = 21,
  xmm1 = 22,
  xmm2 = 23,
  xmm3 = 24,
  xmm4 = 25,
  xmm5 = 26,
  xmm6 = 27,
  xmm7 = 28,

  // --- TLS & System Extensions ---
  gs_base = 58,
  fs_base = 59,
  cr0 = 100,
  cr3 = 103,
  cr4 = 104
};

struct register_traits {
  inline static constexpr microfmt::array gpr_registers{
      register_descriptor{"EAX", eax}, register_descriptor{"ECX", ecx},
      register_descriptor{"EDX", edx}, register_descriptor{"EBX", ebx},
      register_descriptor{"ESP", esp}, register_descriptor{"EBP", ebp},
      register_descriptor{"ESI", esi}, register_descriptor{"EDI", edi},
      register_descriptor{"EIP", eip}};

  [[nodiscard]] static constexpr microfmt::array<register_descriptor, 7>
  address_registers() noexcept {
    return {{{"EBP", ebp}, {"ESI", esi}, {"EDI", edi}, {"EBX", ebx},
             {"EAX", eax}, {"ECX", ecx}, {"EDX", edx}}};
  }

  inline static constexpr microfmt::array system_registers{
      register_descriptor{"GS_BASE", gs_base},
      register_descriptor{"FS_BASE", fs_base},
      register_descriptor{"CR0", cr0},
      register_descriptor{"CR3", cr3},
      register_descriptor{"CR4", cr4}};
};
} // namespace x86

// ============================================================================
// AArch64 (ARM 64-bit) DWARF Register Numbers
// ============================================================================
namespace aarch64 {
enum : uint32_t {
  x0 = 0,
  x1 = 1,
  x2 = 2,
  x3 = 3,
  x4 = 4,
  x5 = 5,
  x6 = 6,
  x7 = 7,
  x8 = 8,
  x9 = 9,
  x10 = 10,
  x11 = 11,
  x12 = 12,
  x13 = 13,
  x14 = 14,
  x15 = 15,
  x16 = 16,
  x17 = 17,
  x18 = 18,
  x19 = 19,
  x20 = 20,
  x21 = 21,
  x22 = 22,
  x23 = 23,
  x24 = 24,
  x25 = 25,
  x26 = 26,
  x27 = 27,
  x28 = 28,
  x29 = 29,
  x30 = 30,
  sp = 31,
  pc = 32,

  // Semantic Platform Aliases
  fp = x29,
  lr = x30,

  // Floating Point & SIMD / Neon Vector Registers (V0 - V31)
  v0 = 64,
  v1 = 65,
  v2 = 66,
  v3 = 67,
  v4 = 68,
  v5 = 69,
  v6 = 70,
  v7 = 71,
  v8 = 72,
  v9 = 73,
  v10 = 74,
  v11 = 75,
  v12 = 76,
  v13 = 77,
  v14 = 78,
  v15 = 79,
  v16 = 80,
  v17 = 81,
  v18 = 82,
  v19 = 83,
  v20 = 84,
  v21 = 85,
  v22 = 86,
  v23 = 87,
  v24 = 88,
  v25 = 89,
  v26 = 90,
  v27 = 91,
  v28 = 92,
  v29 = 93,
  v30 = 94,
  v31 = 95,

  // --- Kernel & Hypervisor System Registers (Extended IDs) ---
  tpidr_el0 = 256,   // User TLS
  tpidrro_el0 = 257, // User Read-Only TLS
  tpidr_el1 = 258,   // Kernel TLS (OS thread structure pointer)
  tpidr_el2 = 259,   // Hypervisor TLS
  tpidr_el3 = 260,   // Monitor TLS
  sp_el0 = 261,
  sp_el1 = 262,
  elr_el1 = 263,   // Exception Link Register EL1
  spsr_el1 = 264,  // Saved Program Status Register EL1
  sctlr_el1 = 265, // System Control Register EL1
  vbar_el1 = 266,  // Vector Base Address Register EL1
  vbar_el2 = 267,  // Vector Base Address Register EL2
  sp_el2 = 268,
  sp_el3 = 269,
  elr_el2 = 270,
  elr_el3 = 271,
  spsr_el2 = 272,
  spsr_el3 = 273,
  sctlr_el2 = 274,
  sctlr_el3 = 275,
  vbar_el3 = 276,
  ttbr0_el1 = 277,
  ttbr1_el1 = 278,
  tcr_el1 = 279,
  mair_el1 = 280,
  amair_el1 = 281,
  esr_el1 = 282,
  far_el1 = 283,
  par_el1 = 284,
  contextidr_el1 = 285,
  cpacr_el1 = 286,
  midr_el1 = 287,
  mpidr_el1 = 288,
  revidr_el1 = 289,
  id_aa64pfr0_el1 = 290,
  id_aa64mmfr0_el1 = 291,
  id_aa64isar0_el1 = 292,
  ttbr0_el2 = 293,
  tcr_el2 = 294,
  mair_el2 = 295,
  esr_el2 = 296,
  far_el2 = 297,
  hcr_el2 = 298,
  vtcr_el2 = 299,
  vttbr_el2 = 300,
  scr_el3 = 301,
  esr_el3 = 302,
  far_el3 = 303,
  pstate = 304,
  currentel = 305,
  daif = 306,
  nzcv = 307,

  cntfrq_el0 = generic_timer::cntfrq,
  cntpct_el0 = generic_timer::cntpct,
  cntvct_el0 = generic_timer::cntvct,
  cntp_tval_el0 = generic_timer::cntp_tval,
  cntp_ctl_el0 = generic_timer::cntp_ctl,
  cntp_cval_el0 = generic_timer::cntp_cval,
  cntv_tval_el0 = generic_timer::cntv_tval,
  cntv_ctl_el0 = generic_timer::cntv_ctl,
  cntv_cval_el0 = generic_timer::cntv_cval,
  cnthp_tval_el2 = generic_timer::cnthp_tval,
  cnthp_ctl_el2 = generic_timer::cnthp_ctl,
  cnthp_cval_el2 = generic_timer::cnthp_cval,
  cntvoff_el2 = generic_timer::cntvoff,
  cnthctl_el2 = generic_timer::cnthctl,
  cnthv_tval_el2 = generic_timer::cnthv_tval,
  cnthv_ctl_el2 = generic_timer::cnthv_ctl,
  cnthv_cval_el2 = generic_timer::cnthv_cval,
  cnthps_tval_el2 = generic_timer::cnthps_tval,
  cnthps_ctl_el2 = generic_timer::cnthps_ctl,
  cnthps_cval_el2 = generic_timer::cnthps_cval,
  cnthvs_tval_el2 = generic_timer::cnthvs_tval,
  cnthvs_ctl_el2 = generic_timer::cnthvs_ctl,
  cnthvs_cval_el2 = generic_timer::cnthvs_cval,
  cntkctl_el1 = generic_timer::cntkctl
};

struct register_traits {
  inline static constexpr microfmt::array gpr_registers{
      register_descriptor{"X0", x0},   register_descriptor{"X1", x1},
      register_descriptor{"X2", x2},   register_descriptor{"X3", x3},
      register_descriptor{"X4", x4},   register_descriptor{"X5", x5},
      register_descriptor{"X6", x6},   register_descriptor{"X7", x7},
      register_descriptor{"X8", x8},   register_descriptor{"X9", x9},
      register_descriptor{"X10", x10}, register_descriptor{"X11", x11},
      register_descriptor{"X12", x12}, register_descriptor{"X13", x13},
      register_descriptor{"X14", x14}, register_descriptor{"X15", x15},
      register_descriptor{"X16", x16}, register_descriptor{"X17", x17},
      register_descriptor{"X18", x18}, register_descriptor{"X19", x19},
      register_descriptor{"X20", x20}, register_descriptor{"X21", x21},
      register_descriptor{"X22", x22}, register_descriptor{"X23", x23},
      register_descriptor{"X24", x24}, register_descriptor{"X25", x25},
      register_descriptor{"X26", x26}, register_descriptor{"X27", x27},
      register_descriptor{"X28", x28}, register_descriptor{"FP", fp},
      register_descriptor{"LR", lr},   register_descriptor{"SP", sp},
      register_descriptor{"PC", pc}};

  [[nodiscard]] static constexpr microfmt::array<register_descriptor, 30>
  address_registers() noexcept {
    return {{{"FP", fp},   {"X0", x0},   {"X1", x1},   {"X2", x2},
             {"X3", x3},   {"X4", x4},   {"X5", x5},   {"X6", x6},
             {"X7", x7},   {"X19", x19}, {"X20", x20}, {"X21", x21},
             {"X22", x22}, {"X23", x23}, {"X24", x24}, {"X25", x25},
             {"X26", x26}, {"X27", x27}, {"X28", x28}, {"X8", x8},
             {"X9", x9},   {"X10", x10}, {"X11", x11}, {"X12", x12},
             {"X13", x13}, {"X14", x14}, {"X15", x15}, {"X16", x16},
             {"X17", x17}, {"X18", x18}}};
  }

  inline static constexpr microfmt::array system_registers{
      register_descriptor{"TPIDR_EL0", tpidr_el0},
      register_descriptor{"TPIDRRO_EL0", tpidrro_el0},
      register_descriptor{"TPIDR_EL1", tpidr_el1},
      register_descriptor{"TPIDR_EL2", tpidr_el2},
      register_descriptor{"TPIDR_EL3", tpidr_el3},
      register_descriptor{"SP_EL0", sp_el0},
      register_descriptor{"SP_EL1", sp_el1},
      register_descriptor{"SP_EL2", sp_el2},
      register_descriptor{"SP_EL3", sp_el3},
      register_descriptor{"ELR_EL1", elr_el1},
      register_descriptor{"ELR_EL2", elr_el2},
      register_descriptor{"ELR_EL3", elr_el3},
      register_descriptor{"SPSR_EL1", spsr_el1},
      register_descriptor{"SPSR_EL2", spsr_el2},
      register_descriptor{"SPSR_EL3", spsr_el3},
      register_descriptor{"SCTLR_EL1", sctlr_el1},
      register_descriptor{"SCTLR_EL2", sctlr_el2},
      register_descriptor{"SCTLR_EL3", sctlr_el3},
      register_descriptor{"VBAR_EL1", vbar_el1},
      register_descriptor{"VBAR_EL2", vbar_el2},
      register_descriptor{"VBAR_EL3", vbar_el3},
      register_descriptor{"TTBR0_EL1", ttbr0_el1},
      register_descriptor{"TTBR1_EL1", ttbr1_el1},
      register_descriptor{"TCR_EL1", tcr_el1},
      register_descriptor{"MAIR_EL1", mair_el1},
      register_descriptor{"AMAIR_EL1", amair_el1},
      register_descriptor{"ESR_EL1", esr_el1},
      register_descriptor{"FAR_EL1", far_el1},
      register_descriptor{"PAR_EL1", par_el1},
      register_descriptor{"CONTEXTIDR_EL1", contextidr_el1},
      register_descriptor{"CPACR_EL1", cpacr_el1},
      register_descriptor{"MIDR_EL1", midr_el1},
      register_descriptor{"MPIDR_EL1", mpidr_el1},
      register_descriptor{"REVIDR_EL1", revidr_el1},
      register_descriptor{"ID_AA64PFR0_EL1", id_aa64pfr0_el1},
      register_descriptor{"ID_AA64MMFR0_EL1", id_aa64mmfr0_el1},
      register_descriptor{"ID_AA64ISAR0_EL1", id_aa64isar0_el1},
      register_descriptor{"TTBR0_EL2", ttbr0_el2},
      register_descriptor{"TCR_EL2", tcr_el2},
      register_descriptor{"MAIR_EL2", mair_el2},
      register_descriptor{"ESR_EL2", esr_el2},
      register_descriptor{"FAR_EL2", far_el2},
      register_descriptor{"HCR_EL2", hcr_el2},
      register_descriptor{"VTCR_EL2", vtcr_el2},
      register_descriptor{"VTTBR_EL2", vttbr_el2},
      register_descriptor{"SCR_EL3", scr_el3},
      register_descriptor{"ESR_EL3", esr_el3},
      register_descriptor{"FAR_EL3", far_el3},
      register_descriptor{"PSTATE", pstate},
      register_descriptor{"CurrentEL", currentel},
      register_descriptor{"DAIF", daif},
      register_descriptor{"NZCV", nzcv},
      register_descriptor{"CNTFRQ_EL0", cntfrq_el0},
      register_descriptor{"CNTPCT_EL0", cntpct_el0},
      register_descriptor{"CNTVCT_EL0", cntvct_el0},
      register_descriptor{"CNTP_TVAL_EL0", cntp_tval_el0},
      register_descriptor{"CNTP_CTL_EL0", cntp_ctl_el0},
      register_descriptor{"CNTP_CVAL_EL0", cntp_cval_el0},
      register_descriptor{"CNTV_TVAL_EL0", cntv_tval_el0},
      register_descriptor{"CNTV_CTL_EL0", cntv_ctl_el0},
      register_descriptor{"CNTV_CVAL_EL0", cntv_cval_el0},
      register_descriptor{"CNTHP_TVAL_EL2", cnthp_tval_el2},
      register_descriptor{"CNTHP_CTL_EL2", cnthp_ctl_el2},
      register_descriptor{"CNTHP_CVAL_EL2", cnthp_cval_el2},
      register_descriptor{"CNTVOFF_EL2", cntvoff_el2},
      register_descriptor{"CNTHCTL_EL2", cnthctl_el2},
      register_descriptor{"CNTHV_TVAL_EL2", cnthv_tval_el2},
      register_descriptor{"CNTHV_CTL_EL2", cnthv_ctl_el2},
      register_descriptor{"CNTHV_CVAL_EL2", cnthv_cval_el2},
      register_descriptor{"CNTHPS_TVAL_EL2", cnthps_tval_el2},
      register_descriptor{"CNTHPS_CTL_EL2", cnthps_ctl_el2},
      register_descriptor{"CNTHPS_CVAL_EL2", cnthps_cval_el2},
      register_descriptor{"CNTHVS_TVAL_EL2", cnthvs_tval_el2},
      register_descriptor{"CNTHVS_CTL_EL2", cnthvs_ctl_el2},
      register_descriptor{"CNTHVS_CVAL_EL2", cnthvs_cval_el2},
      register_descriptor{"CNTKCTL_EL1", cntkctl_el1}};
};
} // namespace aarch64

// ============================================================================
// ARM32 (AArch32) DWARF Register Numbers
// ============================================================================
namespace arm32 {
enum : uint32_t {
  r0 = 0,
  r1 = 1,
  r2 = 2,
  r3 = 3,
  r4 = 4,
  r5 = 5,
  r6 = 6,
  r7 = 7,
  r8 = 8,
  r9 = 9,
  r10 = 10,
  r11 = 11,
  r12 = 12,
  r13 = 13,
  r14 = 14,
  r15 = 15,
  // Semantic Platform Aliases
  fp = r11,
  sp = r13,
  lr = r14,
  pc = r15,

  // VFP / NEON Double-Precision Floating Point Registers (D0 - D31)
  d0 = 64,
  d1 = 65,
  d2 = 66,
  d3 = 67,
  d4 = 68,
  d5 = 69,
  d6 = 70,
  d7 = 71,
  d8 = 72,
  d9 = 73,
  d10 = 74,
  d11 = 75,
  d12 = 76,
  d13 = 77,
  d14 = 78,
  d15 = 79,
  d16 = 80,
  d17 = 81,
  d18 = 82,
  d19 = 83,
  d20 = 84,
  d21 = 85,
  d22 = 86,
  d23 = 87,
  d24 = 88,
  d25 = 89,
  d26 = 90,
  d27 = 91,
  d28 = 92,
  d29 = 93,
  d30 = 94,
  d31 = 95,

  // --- Kernel & Privileged System Registers (Extended IDs) ---
  tpidrurw = 256, // User Read/Write Thread ID
  tpidruro = 257, // User Read-Only Thread ID
  tpidrprw = 258, // Privileged Read/Write Thread ID (Kernel thread pointer)
  cpsr = 259,     // Current Program Status Register
  spsr = 260,     // Saved Program Status Register (Exception mode)
  apsr = 261,
  iapsr = 262,
  eapsr = 263,
  xpsr = 264,
  ipsr = 265,
  epsr = 266,
  iepsr = 267,
  msp = 268,
  psp = 269,
  primask = 270,
  basepri = 271,
  basepri_max = 272,
  faultmask = 273,
  control = 274,
  sctlr = 275,
  actlr = 276,
  cpacr = 277,
  ttbr0 = 278,
  ttbr1 = 279,
  ttbcr = 280,
  dacr = 281,
  dfsr = 282,
  ifsr = 283,
  dfar = 284,
  ifar = 285,
  vbar = 286,
  contextidr = 287,
  mair0 = 288,
  mair1 = 289,
  amair0 = 290,
  amair1 = 291,
  midr = 292,
  mpidr = 293,

  cntfrq = generic_timer::cntfrq,
  cntpct = generic_timer::cntpct,
  cntvct = generic_timer::cntvct,
  cntp_tval = generic_timer::cntp_tval,
  cntp_ctl = generic_timer::cntp_ctl,
  cntp_cval = generic_timer::cntp_cval,
  cntv_tval = generic_timer::cntv_tval,
  cntv_ctl = generic_timer::cntv_ctl,
  cntv_cval = generic_timer::cntv_cval,
  cnthp_tval = generic_timer::cnthp_tval,
  cnthp_ctl = generic_timer::cnthp_ctl,
  cnthp_cval = generic_timer::cnthp_cval,
  cntvoff = generic_timer::cntvoff,
  cnthctl = generic_timer::cnthctl,
  cnthv_tval = generic_timer::cnthv_tval,
  cnthv_ctl = generic_timer::cnthv_ctl,
  cnthv_cval = generic_timer::cnthv_cval,
  cnthps_tval = generic_timer::cnthps_tval,
  cnthps_ctl = generic_timer::cnthps_ctl,
  cnthps_cval = generic_timer::cnthps_cval,
  cnthvs_tval = generic_timer::cnthvs_tval,
  cnthvs_ctl = generic_timer::cnthvs_ctl,
  cnthvs_cval = generic_timer::cnthvs_cval,
  cntkctl = generic_timer::cntkctl
};

struct register_traits {
  inline static constexpr microfmt::array gpr_registers{
      register_descriptor{"R0", r0},   register_descriptor{"R1", r1},
      register_descriptor{"R2", r2},   register_descriptor{"R3", r3},
      register_descriptor{"R4", r4},   register_descriptor{"R5", r5},
      register_descriptor{"R6", r6},   register_descriptor{"R7", r7},
      register_descriptor{"R8", r8},   register_descriptor{"R9", r9},
      register_descriptor{"R10", r10}, register_descriptor{"FP", fp},
      register_descriptor{"IP", r12},  register_descriptor{"SP", sp},
      register_descriptor{"LR", lr},   register_descriptor{"PC", pc}};

  [[nodiscard]] static constexpr microfmt::array<register_descriptor, 13>
  address_registers() noexcept {
    return {{{"FP", fp}, {"R0", r0}, {"R1", r1}, {"R2", r2}, {"R3", r3},
             {"R4", r4}, {"R5", r5}, {"R6", r6}, {"R7", r7}, {"R8", r8},
             {"R9", r9}, {"R10", r10}, {"IP", r12}}};
  }

  inline static constexpr microfmt::array system_registers{
      register_descriptor{"TPIDRURW", tpidrurw},
      register_descriptor{"TPIDRURO", tpidruro},
      register_descriptor{"TPIDRPRW", tpidrprw},
      register_descriptor{"CPSR", cpsr},
      register_descriptor{"SPSR", spsr},
      register_descriptor{"APSR", apsr},
      register_descriptor{"IAPSR", iapsr},
      register_descriptor{"EAPSR", eapsr},
      register_descriptor{"XPSR", xpsr},
      register_descriptor{"IPSR", ipsr},
      register_descriptor{"EPSR", epsr},
      register_descriptor{"IEPSR", iepsr},
      register_descriptor{"MSP", msp},
      register_descriptor{"PSP", psp},
      register_descriptor{"PRIMASK", primask},
      register_descriptor{"BASEPRI", basepri},
      register_descriptor{"BASEPRI_MAX", basepri_max},
      register_descriptor{"FAULTMASK", faultmask},
      register_descriptor{"CONTROL", control},
      register_descriptor{"SCTLR", sctlr},
      register_descriptor{"ACTLR", actlr},
      register_descriptor{"CPACR", cpacr},
      register_descriptor{"TTBR0", ttbr0},
      register_descriptor{"TTBR1", ttbr1},
      register_descriptor{"TTBCR", ttbcr},
      register_descriptor{"DACR", dacr},
      register_descriptor{"DFSR", dfsr},
      register_descriptor{"IFSR", ifsr},
      register_descriptor{"DFAR", dfar},
      register_descriptor{"IFAR", ifar},
      register_descriptor{"VBAR", vbar},
      register_descriptor{"CONTEXTIDR", contextidr},
      register_descriptor{"MAIR0", mair0},
      register_descriptor{"MAIR1", mair1},
      register_descriptor{"AMAIR0", amair0},
      register_descriptor{"AMAIR1", amair1},
      register_descriptor{"MIDR", midr},
      register_descriptor{"MPIDR", mpidr},
      register_descriptor{"CNTFRQ", cntfrq},
      register_descriptor{"CNTPCT", cntpct},
      register_descriptor{"CNTVCT", cntvct},
      register_descriptor{"CNTP_TVAL", cntp_tval},
      register_descriptor{"CNTP_CTL", cntp_ctl},
      register_descriptor{"CNTP_CVAL", cntp_cval},
      register_descriptor{"CNTV_TVAL", cntv_tval},
      register_descriptor{"CNTV_CTL", cntv_ctl},
      register_descriptor{"CNTV_CVAL", cntv_cval},
      register_descriptor{"CNTHP_TVAL", cnthp_tval},
      register_descriptor{"CNTHP_CTL", cnthp_ctl},
      register_descriptor{"CNTHP_CVAL", cnthp_cval},
      register_descriptor{"CNTVOFF", cntvoff},
      register_descriptor{"CNTHCTL", cnthctl},
      register_descriptor{"CNTHV_TVAL", cnthv_tval},
      register_descriptor{"CNTHV_CTL", cnthv_ctl},
      register_descriptor{"CNTHV_CVAL", cnthv_cval},
      register_descriptor{"CNTHPS_TVAL", cnthps_tval},
      register_descriptor{"CNTHPS_CTL", cnthps_ctl},
      register_descriptor{"CNTHPS_CVAL", cnthps_cval},
      register_descriptor{"CNTHVS_TVAL", cnthvs_tval},
      register_descriptor{"CNTHVS_CTL", cnthvs_ctl},
      register_descriptor{"CNTHVS_CVAL", cnthvs_cval},
      register_descriptor{"CNTKCTL", cntkctl}};
};
} // namespace arm32

// ============================================================================
// RISC-V (RV32 / RV64) DWARF Register Numbers
// ============================================================================
namespace riscv {
enum : uint32_t {
  // Integer Registers (X0 - X31) with ABI Name Aliases
  x0 = 0,
  zero = 0,
  x1 = 1,
  ra = 1, // Return Address (Link Register)
  x2 = 2,
  sp = 2, // Stack Pointer
  x3 = 3,
  gp = 3, // Global Pointer
  x4 = 4,
  tp = 4, // Thread Pointer
  x5 = 5,
  t0 = 5, // Temporaries
  x6 = 6,
  t1 = 6,
  x7 = 7,
  t2 = 7,
  x8 = 8,
  s0 = 8,
  fp = 8, // Saved Register / Frame Pointer
  x9 = 9,
  s1 = 9,
  x10 = 10,
  a0 = 10, // Function Arguments / Return Values
  x11 = 11,
  a1 = 11,
  x12 = 12,
  a2 = 12,
  x13 = 13,
  a3 = 13,
  x14 = 14,
  a4 = 14,
  x15 = 15,
  a5 = 15,
  x16 = 16,
  a6 = 16,
  x17 = 17,
  a7 = 17,
  x18 = 18,
  s2 = 18, // Saved Registers
  x19 = 19,
  s3 = 19,
  x20 = 20,
  s4 = 20,
  x21 = 21,
  s5 = 21,
  x22 = 22,
  s6 = 22,
  x23 = 23,
  s7 = 23,
  x24 = 24,
  s8 = 24,
  x25 = 25,
  s9 = 25,
  x26 = 26,
  s10 = 26,
  x27 = 27,
  s11 = 27,
  x28 = 28,
  s12 = 28,
  t3 = 28,
  x29 = 29,
  s13 = 29,
  t4 = 29,
  x30 = 30,
  s14 = 30,
  t5 = 30,
  x31 = 31,
  s15 = 31,
  t6 = 31,

  // Floating-Point Registers (F0 - F31)
  f0 = 32,
  f1 = 33,
  f2 = 34,
  f3 = 35,
  f4 = 36,
  f5 = 37,
  f6 = 38,
  f7 = 39,
  f8 = 40,
  f9 = 41,
  f10 = 42,
  f11 = 43,
  f12 = 44,
  f13 = 45,
  f14 = 46,
  f15 = 47,
  f16 = 48,
  f17 = 49,
  f18 = 50,
  f19 = 51,
  f20 = 52,
  f21 = 53,
  f22 = 54,
  f23 = 55,
  f24 = 56,
  f25 = 57,
  f26 = 58,
  f27 = 59,
  f28 = 60,
  f29 = 61,
  f30 = 62,
  f31 = 63,

  // Program Counter
  pc = 65,

  // --- Supervisor & Machine Control Status Registers (CSRs) ---
  sstatus = 256,
  sepc = 257,
  stval = 258,
  satp = 259, // Supervisor Address Translation and Protection (Page Table Base)
  mstatus = 260,
  mepc = 261,
  mtvec = 262
};

struct register_traits {
  inline static constexpr microfmt::array gpr_registers{
      register_descriptor{"zero", zero}, register_descriptor{"ra", ra},
      register_descriptor{"sp", sp},     register_descriptor{"gp", gp},
      register_descriptor{"tp", tp},     register_descriptor{"t0", t0},
      register_descriptor{"t1", t1},     register_descriptor{"t2", t2},
      register_descriptor{"s0", s0},     register_descriptor{"s1", s1},
      register_descriptor{"a0", a0},     register_descriptor{"a1", a1},
      register_descriptor{"a2", a2},     register_descriptor{"a3", a3},
      register_descriptor{"a4", a4},     register_descriptor{"a5", a5},
      register_descriptor{"a6", a6},     register_descriptor{"a7", a7},
      register_descriptor{"s2", s2},     register_descriptor{"s3", s3},
      register_descriptor{"s4", s4},     register_descriptor{"s5", s5},
      register_descriptor{"s6", s6},     register_descriptor{"s7", s7},
      register_descriptor{"s8", s8},     register_descriptor{"s9", s9},
      register_descriptor{"s10", s10},   register_descriptor{"s11", s11},
      register_descriptor{"t3", t3},     register_descriptor{"t4", t4},
      register_descriptor{"t5", t5},     register_descriptor{"t6", t6},
      register_descriptor{"pc", pc}};

  [[nodiscard]] static constexpr microfmt::array<register_descriptor, 29>
  address_registers() noexcept {
    return {{{"s0", fp},  {"gp", gp},  {"tp", tp},   {"a0", a0},
             {"a1", a1},  {"a2", a2},  {"a3", a3},   {"a4", a4},
             {"a5", a5},  {"a6", a6},  {"a7", a7},   {"s1", s1},
             {"s2", s2},  {"s3", s3},  {"s4", s4},   {"s5", s5},
             {"s6", s6},  {"s7", s7},  {"s8", s8},   {"s9", s9},
             {"s10", s10}, {"s11", s11}, {"t0", t0}, {"t1", t1},
             {"t2", t2},  {"t3", t3},  {"t4", t4},   {"t5", t5},
             {"t6", t6}}};
  }

  inline static constexpr microfmt::array system_registers{
      register_descriptor{"sstatus", sstatus},
      register_descriptor{"sepc", sepc},
      register_descriptor{"stval", stval},
      register_descriptor{"satp", satp},
      register_descriptor{"mstatus", mstatus},
      register_descriptor{"mepc", mepc},
      register_descriptor{"mtvec", mtvec}};
};
} // namespace riscv

} // namespace microfmt::dwarf