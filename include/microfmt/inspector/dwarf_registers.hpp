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
  CNTFRQ = 320,
  CNTPCT = 321,
  CNTVCT = 322,
  CNTP_TVAL = 323,
  CNTP_CTL = 324,
  CNTP_CVAL = 325,
  CNTV_TVAL = 326,
  CNTV_CTL = 327,
  CNTV_CVAL = 328,
  CNTHP_TVAL = 329,
  CNTHP_CTL = 330,
  CNTHP_CVAL = 331,
  CNTVOFF = 332,
  CNTHCTL = 333,
  CNTHV_TVAL = 334,
  CNTHV_CTL = 335,
  CNTHV_CVAL = 336,
  CNTHPS_TVAL = 337,
  CNTHPS_CTL = 338,
  CNTHPS_CVAL = 339,
  CNTHVS_TVAL = 340,
  CNTHVS_CTL = 341,
  CNTHVS_CVAL = 342,
  CNTKCTL = 343
};
} // namespace generic_timer

// ============================================================================
// x86_64 (AMD64) DWARF Register Numbers (System V ABI)
// ============================================================================
namespace x86_64 {
enum : uint32_t {
  RAX = 0,
  RDX = 1,
  RCX = 2,
  RBX = 3,
  RSI = 4,
  RDI = 5,
  RBP = 6,
  RSP = 7,
  R8 = 8,
  R9 = 9,
  R10 = 10,
  R11 = 11,
  R12 = 12,
  R13 = 13,
  R14 = 14,
  R15 = 15,
  RIP = 16,

  // Semantic Platform Aliases
  FP = RBP,
  SP = RSP,
  PC = RIP,

  // Floating Point & SSE/AVX Vector Registers (XMM0 - XMM15)
  XMM0 = 17,
  XMM1 = 18,
  XMM2 = 19,
  XMM3 = 20,
  XMM4 = 21,
  XMM5 = 22,
  XMM6 = 23,
  XMM7 = 24,
  XMM8 = 25,
  XMM9 = 26,
  XMM10 = 27,
  XMM11 = 28,
  XMM12 = 29,
  XMM13 = 30,
  XMM14 = 31,
  XMM15 = 32,

  // Status & Control
  EFLAGS = 49,

  // --- Segments & Base Registers ---
  FS_BASE = 58,
  GS_BASE = 59,
  CS = 60,
  SS = 61,
  DS = 62,
  ES = 63,
  FS = 64,
  GS = 65,

  // --- Kernel / Privileged & Debug Registers (Extended IDs) ---
  KERNEL_GS_BASE = 66, // MSR IA32_KERNEL_GS_BASE (SwapGS target)
  CR0 = 100,
  CR2 = 102,
  CR3 = 103, // Page table base (PML4 / CR3)
  CR4 = 104,
  CR8 = 108, // Task Priority Register (TPR)
  DR0 = 120, // Hardware Breakpoints / Debug Registers
  DR1 = 121,
  DR2 = 122,
  DR3 = 123,
  DR6 = 126, // Debug Status
  DR7 = 127  // Debug Control
};

struct register_traits {
  inline static constexpr microfmt::array gpr_registers{
      register_descriptor{"RAX", RAX}, register_descriptor{"RCX", RCX},
      register_descriptor{"RDX", RDX}, register_descriptor{"RBX", RBX},
      register_descriptor{"RSP", RSP}, register_descriptor{"RBP", RBP},
      register_descriptor{"RSI", RSI}, register_descriptor{"RDI", RDI},
      register_descriptor{"R8", R8},   register_descriptor{"R9", R9},
      register_descriptor{"R10", R10}, register_descriptor{"R11", R11},
      register_descriptor{"R12", R12}, register_descriptor{"R13", R13},
      register_descriptor{"R14", R14}, register_descriptor{"R15", R15},
      register_descriptor{"RIP", RIP}};

  [[nodiscard]] static constexpr microfmt::array<register_descriptor, 15>
  address_registers() noexcept {
    return {{{"RBP", RBP}, {"RDI", RDI}, {"RSI", RSI}, {"RBX", RBX},
             {"R12", R12}, {"R13", R13}, {"R14", R14}, {"R15", R15},
             {"RAX", RAX}, {"RCX", RCX}, {"RDX", RDX}, {"R8", R8},
             {"R9", R9}, {"R10", R10}, {"R11", R11}}};
  }

  inline static constexpr microfmt::array system_registers{
      register_descriptor{"EFLAGS", EFLAGS},
      register_descriptor{"FS_BASE", FS_BASE},
      register_descriptor{"GS_BASE", GS_BASE},
      register_descriptor{"KERNEL_GS_BASE", KERNEL_GS_BASE},
      register_descriptor{"CS", CS},
      register_descriptor{"SS", SS},
      register_descriptor{"DS", DS},
      register_descriptor{"ES", ES},
      register_descriptor{"FS", FS},
      register_descriptor{"GS", GS},
      register_descriptor{"CR0", CR0},
      register_descriptor{"CR2", CR2},
      register_descriptor{"CR3", CR3},
      register_descriptor{"CR4", CR4},
      register_descriptor{"CR8", CR8},
      register_descriptor{"DR0", DR0},
      register_descriptor{"DR1", DR1},
      register_descriptor{"DR2", DR2},
      register_descriptor{"DR3", DR3},
      register_descriptor{"DR6", DR6},
      register_descriptor{"DR7", DR7}};
};
} // namespace x86_64

// ============================================================================
// x86 (IA-32) DWARF Register Numbers
// ============================================================================
namespace x86 {
enum : uint32_t {
  EAX = 0,
  ECX = 1,
  EDX = 2,
  EBX = 3,
  ESP = 4,
  EBP = 5,
  ESI = 6,
  EDI = 7,
  EIP = 8,

  // Semantic Platform Aliases
  FP = EBP,
  SP = ESP,
  PC = EIP,

  // Floating Point Stack (ST0 - ST7)
  ST0 = 11,
  ST1 = 12,
  ST2 = 13,
  ST3 = 14,
  ST4 = 15,
  ST5 = 16,
  ST6 = 17,
  ST7 = 18,

  // SSE Vector Registers (XMM0 - XMM7)
  XMM0 = 21,
  XMM1 = 22,
  XMM2 = 23,
  XMM3 = 24,
  XMM4 = 25,
  XMM5 = 26,
  XMM6 = 27,
  XMM7 = 28,

  // --- TLS & System Extensions ---
  GS_BASE = 58,
  FS_BASE = 59,
  CR0 = 100,
  CR3 = 103,
  CR4 = 104
};

struct register_traits {
  inline static constexpr microfmt::array gpr_registers{
      register_descriptor{"EAX", EAX}, register_descriptor{"ECX", ECX},
      register_descriptor{"EDX", EDX}, register_descriptor{"EBX", EBX},
      register_descriptor{"ESP", ESP}, register_descriptor{"EBP", EBP},
      register_descriptor{"ESI", ESI}, register_descriptor{"EDI", EDI},
      register_descriptor{"EIP", EIP}};

  [[nodiscard]] static constexpr microfmt::array<register_descriptor, 7>
  address_registers() noexcept {
    return {{{"EBP", EBP}, {"ESI", ESI}, {"EDI", EDI}, {"EBX", EBX},
             {"EAX", EAX}, {"ECX", ECX}, {"EDX", EDX}}};
  }

  inline static constexpr microfmt::array system_registers{
      register_descriptor{"GS_BASE", GS_BASE},
      register_descriptor{"FS_BASE", FS_BASE},
      register_descriptor{"CR0", CR0},
      register_descriptor{"CR3", CR3},
      register_descriptor{"CR4", CR4}};
};
} // namespace x86

// ============================================================================
// AArch64 (ARM 64-bit) DWARF Register Numbers
// ============================================================================
namespace aarch64 {
enum : uint32_t {
  X0 = 0,
  X1 = 1,
  X2 = 2,
  X3 = 3,
  X4 = 4,
  X5 = 5,
  X6 = 6,
  X7 = 7,
  X8 = 8,
  X9 = 9,
  X10 = 10,
  X11 = 11,
  X12 = 12,
  X13 = 13,
  X14 = 14,
  X15 = 15,
  X16 = 16,
  X17 = 17,
  X18 = 18,
  X19 = 19,
  X20 = 20,
  X21 = 21,
  X22 = 22,
  X23 = 23,
  X24 = 24,
  X25 = 25,
  X26 = 26,
  X27 = 27,
  X28 = 28,
  X29 = 29,
  X30 = 30,
  SP = 31,
  PC = 32,

  // Semantic Platform Aliases
  FP = X29,
  LR = X30,

  // Floating Point & SIMD / Neon Vector Registers (V0 - V31)
  V0 = 64,
  V1 = 65,
  V2 = 66,
  V3 = 67,
  V4 = 68,
  V5 = 69,
  V6 = 70,
  V7 = 71,
  V8 = 72,
  V9 = 73,
  V10 = 74,
  V11 = 75,
  V12 = 76,
  V13 = 77,
  V14 = 78,
  V15 = 79,
  V16 = 80,
  V17 = 81,
  V18 = 82,
  V19 = 83,
  V20 = 84,
  V21 = 85,
  V22 = 86,
  V23 = 87,
  V24 = 88,
  V25 = 89,
  V26 = 90,
  V27 = 91,
  V28 = 92,
  V29 = 93,
  V30 = 94,
  V31 = 95,

  // --- Kernel & Hypervisor System Registers (Extended IDs) ---
  TPIDR_EL0 = 256,   // User TLS
  TPIDRRO_EL0 = 257, // User Read-Only TLS
  TPIDR_EL1 = 258,   // Kernel TLS (OS thread structure pointer)
  TPIDR_EL2 = 259,   // Hypervisor TLS
  TPIDR_EL3 = 260,   // Monitor TLS
  SP_EL0 = 261,
  SP_EL1 = 262,
  ELR_EL1 = 263,   // Exception Link Register EL1
  SPSR_EL1 = 264,  // Saved Program Status Register EL1
  SCTLR_EL1 = 265, // System Control Register EL1
  VBAR_EL1 = 266,  // Vector Base Address Register EL1
  VBAR_EL2 = 267,  // Vector Base Address Register EL2
  SP_EL2 = 268,
  SP_EL3 = 269,
  ELR_EL2 = 270,
  ELR_EL3 = 271,
  SPSR_EL2 = 272,
  SPSR_EL3 = 273,
  SCTLR_EL2 = 274,
  SCTLR_EL3 = 275,
  VBAR_EL3 = 276,
  TTBR0_EL1 = 277,
  TTBR1_EL1 = 278,
  TCR_EL1 = 279,
  MAIR_EL1 = 280,
  AMAIR_EL1 = 281,
  ESR_EL1 = 282,
  FAR_EL1 = 283,
  PAR_EL1 = 284,
  CONTEXTIDR_EL1 = 285,
  CPACR_EL1 = 286,
  MIDR_EL1 = 287,
  MPIDR_EL1 = 288,
  REVIDR_EL1 = 289,
  ID_AA64PFR0_EL1 = 290,
  ID_AA64MMFR0_EL1 = 291,
  ID_AA64ISAR0_EL1 = 292,
  TTBR0_EL2 = 293,
  TCR_EL2 = 294,
  MAIR_EL2 = 295,
  ESR_EL2 = 296,
  FAR_EL2 = 297,
  HCR_EL2 = 298,
  VTCR_EL2 = 299,
  VTTBR_EL2 = 300,
  SCR_EL3 = 301,
  ESR_EL3 = 302,
  FAR_EL3 = 303,
  PSTATE = 304,
  CURRENTEL = 305,
  DAIF = 306,
  NZCV = 307,

  CNTFRQ_EL0 = generic_timer::CNTFRQ,
  CNTPCT_EL0 = generic_timer::CNTPCT,
  CNTVCT_EL0 = generic_timer::CNTVCT,
  CNTP_TVAL_EL0 = generic_timer::CNTP_TVAL,
  CNTP_CTL_EL0 = generic_timer::CNTP_CTL,
  CNTP_CVAL_EL0 = generic_timer::CNTP_CVAL,
  CNTV_TVAL_EL0 = generic_timer::CNTV_TVAL,
  CNTV_CTL_EL0 = generic_timer::CNTV_CTL,
  CNTV_CVAL_EL0 = generic_timer::CNTV_CVAL,
  CNTHP_TVAL_EL2 = generic_timer::CNTHP_TVAL,
  CNTHP_CTL_EL2 = generic_timer::CNTHP_CTL,
  CNTHP_CVAL_EL2 = generic_timer::CNTHP_CVAL,
  CNTVOFF_EL2 = generic_timer::CNTVOFF,
  CNTHCTL_EL2 = generic_timer::CNTHCTL,
  CNTHV_TVAL_EL2 = generic_timer::CNTHV_TVAL,
  CNTHV_CTL_EL2 = generic_timer::CNTHV_CTL,
  CNTHV_CVAL_EL2 = generic_timer::CNTHV_CVAL,
  CNTHPS_TVAL_EL2 = generic_timer::CNTHPS_TVAL,
  CNTHPS_CTL_EL2 = generic_timer::CNTHPS_CTL,
  CNTHPS_CVAL_EL2 = generic_timer::CNTHPS_CVAL,
  CNTHVS_TVAL_EL2 = generic_timer::CNTHVS_TVAL,
  CNTHVS_CTL_EL2 = generic_timer::CNTHVS_CTL,
  CNTHVS_CVAL_EL2 = generic_timer::CNTHVS_CVAL,
  CNTKCTL_EL1 = generic_timer::CNTKCTL
};

struct register_traits {
  inline static constexpr microfmt::array gpr_registers{
      register_descriptor{"X0", X0},   register_descriptor{"X1", X1},
      register_descriptor{"X2", X2},   register_descriptor{"X3", X3},
      register_descriptor{"X4", X4},   register_descriptor{"X5", X5},
      register_descriptor{"X6", X6},   register_descriptor{"X7", X7},
      register_descriptor{"X8", X8},   register_descriptor{"X9", X9},
      register_descriptor{"X10", X10}, register_descriptor{"X11", X11},
      register_descriptor{"X12", X12}, register_descriptor{"X13", X13},
      register_descriptor{"X14", X14}, register_descriptor{"X15", X15},
      register_descriptor{"X16", X16}, register_descriptor{"X17", X17},
      register_descriptor{"X18", X18}, register_descriptor{"X19", X19},
      register_descriptor{"X20", X20}, register_descriptor{"X21", X21},
      register_descriptor{"X22", X22}, register_descriptor{"X23", X23},
      register_descriptor{"X24", X24}, register_descriptor{"X25", X25},
      register_descriptor{"X26", X26}, register_descriptor{"X27", X27},
      register_descriptor{"X28", X28}, register_descriptor{"FP", FP},
      register_descriptor{"LR", LR},   register_descriptor{"SP", SP},
      register_descriptor{"PC", PC}};

  [[nodiscard]] static constexpr microfmt::array<register_descriptor, 30>
  address_registers() noexcept {
    return {{{"FP", FP},   {"X0", X0},   {"X1", X1},   {"X2", X2},
             {"X3", X3},   {"X4", X4},   {"X5", X5},   {"X6", X6},
             {"X7", X7},   {"X19", X19}, {"X20", X20}, {"X21", X21},
             {"X22", X22}, {"X23", X23}, {"X24", X24}, {"X25", X25},
             {"X26", X26}, {"X27", X27}, {"X28", X28}, {"X8", X8},
             {"X9", X9},   {"X10", X10}, {"X11", X11}, {"X12", X12},
             {"X13", X13}, {"X14", X14}, {"X15", X15}, {"X16", X16},
             {"X17", X17}, {"X18", X18}}};
  }

  inline static constexpr microfmt::array system_registers{
      register_descriptor{"TPIDR_EL0", TPIDR_EL0},
      register_descriptor{"TPIDRRO_EL0", TPIDRRO_EL0},
      register_descriptor{"TPIDR_EL1", TPIDR_EL1},
      register_descriptor{"TPIDR_EL2", TPIDR_EL2},
      register_descriptor{"TPIDR_EL3", TPIDR_EL3},
      register_descriptor{"SP_EL0", SP_EL0},
      register_descriptor{"SP_EL1", SP_EL1},
      register_descriptor{"SP_EL2", SP_EL2},
      register_descriptor{"SP_EL3", SP_EL3},
      register_descriptor{"ELR_EL1", ELR_EL1},
      register_descriptor{"ELR_EL2", ELR_EL2},
      register_descriptor{"ELR_EL3", ELR_EL3},
      register_descriptor{"SPSR_EL1", SPSR_EL1},
      register_descriptor{"SPSR_EL2", SPSR_EL2},
      register_descriptor{"SPSR_EL3", SPSR_EL3},
      register_descriptor{"SCTLR_EL1", SCTLR_EL1},
      register_descriptor{"SCTLR_EL2", SCTLR_EL2},
      register_descriptor{"SCTLR_EL3", SCTLR_EL3},
      register_descriptor{"VBAR_EL1", VBAR_EL1},
      register_descriptor{"VBAR_EL2", VBAR_EL2},
      register_descriptor{"VBAR_EL3", VBAR_EL3},
      register_descriptor{"TTBR0_EL1", TTBR0_EL1},
      register_descriptor{"TTBR1_EL1", TTBR1_EL1},
      register_descriptor{"TCR_EL1", TCR_EL1},
      register_descriptor{"MAIR_EL1", MAIR_EL1},
      register_descriptor{"AMAIR_EL1", AMAIR_EL1},
      register_descriptor{"ESR_EL1", ESR_EL1},
      register_descriptor{"FAR_EL1", FAR_EL1},
      register_descriptor{"PAR_EL1", PAR_EL1},
      register_descriptor{"CONTEXTIDR_EL1", CONTEXTIDR_EL1},
      register_descriptor{"CPACR_EL1", CPACR_EL1},
      register_descriptor{"MIDR_EL1", MIDR_EL1},
      register_descriptor{"MPIDR_EL1", MPIDR_EL1},
      register_descriptor{"REVIDR_EL1", REVIDR_EL1},
      register_descriptor{"ID_AA64PFR0_EL1", ID_AA64PFR0_EL1},
      register_descriptor{"ID_AA64MMFR0_EL1", ID_AA64MMFR0_EL1},
      register_descriptor{"ID_AA64ISAR0_EL1", ID_AA64ISAR0_EL1},
      register_descriptor{"TTBR0_EL2", TTBR0_EL2},
      register_descriptor{"TCR_EL2", TCR_EL2},
      register_descriptor{"MAIR_EL2", MAIR_EL2},
      register_descriptor{"ESR_EL2", ESR_EL2},
      register_descriptor{"FAR_EL2", FAR_EL2},
      register_descriptor{"HCR_EL2", HCR_EL2},
      register_descriptor{"VTCR_EL2", VTCR_EL2},
      register_descriptor{"VTTBR_EL2", VTTBR_EL2},
      register_descriptor{"SCR_EL3", SCR_EL3},
      register_descriptor{"ESR_EL3", ESR_EL3},
      register_descriptor{"FAR_EL3", FAR_EL3},
      register_descriptor{"PSTATE", PSTATE},
      register_descriptor{"CurrentEL", CURRENTEL},
      register_descriptor{"DAIF", DAIF},
      register_descriptor{"NZCV", NZCV},
      register_descriptor{"CNTFRQ_EL0", CNTFRQ_EL0},
      register_descriptor{"CNTPCT_EL0", CNTPCT_EL0},
      register_descriptor{"CNTVCT_EL0", CNTVCT_EL0},
      register_descriptor{"CNTP_TVAL_EL0", CNTP_TVAL_EL0},
      register_descriptor{"CNTP_CTL_EL0", CNTP_CTL_EL0},
      register_descriptor{"CNTP_CVAL_EL0", CNTP_CVAL_EL0},
      register_descriptor{"CNTV_TVAL_EL0", CNTV_TVAL_EL0},
      register_descriptor{"CNTV_CTL_EL0", CNTV_CTL_EL0},
      register_descriptor{"CNTV_CVAL_EL0", CNTV_CVAL_EL0},
      register_descriptor{"CNTHP_TVAL_EL2", CNTHP_TVAL_EL2},
      register_descriptor{"CNTHP_CTL_EL2", CNTHP_CTL_EL2},
      register_descriptor{"CNTHP_CVAL_EL2", CNTHP_CVAL_EL2},
      register_descriptor{"CNTVOFF_EL2", CNTVOFF_EL2},
      register_descriptor{"CNTHCTL_EL2", CNTHCTL_EL2},
      register_descriptor{"CNTHV_TVAL_EL2", CNTHV_TVAL_EL2},
      register_descriptor{"CNTHV_CTL_EL2", CNTHV_CTL_EL2},
      register_descriptor{"CNTHV_CVAL_EL2", CNTHV_CVAL_EL2},
      register_descriptor{"CNTHPS_TVAL_EL2", CNTHPS_TVAL_EL2},
      register_descriptor{"CNTHPS_CTL_EL2", CNTHPS_CTL_EL2},
      register_descriptor{"CNTHPS_CVAL_EL2", CNTHPS_CVAL_EL2},
      register_descriptor{"CNTHVS_TVAL_EL2", CNTHVS_TVAL_EL2},
      register_descriptor{"CNTHVS_CTL_EL2", CNTHVS_CTL_EL2},
      register_descriptor{"CNTHVS_CVAL_EL2", CNTHVS_CVAL_EL2},
      register_descriptor{"CNTKCTL_EL1", CNTKCTL_EL1}};
};
} // namespace aarch64

// ============================================================================
// ARM32 (AArch32) DWARF Register Numbers
// ============================================================================
namespace arm32 {
enum : uint32_t {
  R0 = 0,
  R1 = 1,
  R2 = 2,
  R3 = 3,
  R4 = 4,
  R5 = 5,
  R6 = 6,
  R7 = 7,
  R8 = 8,
  R9 = 9,
  R10 = 10,
  R11 = 11,
  R12 = 12,
  R13 = 13,
  R14 = 14,
  R15 = 15,
  // Semantic Platform Aliases
  FP = R11,
  SP = R13,
  LR = R14,
  PC = R15,

  // VFP / NEON Double-Precision Floating Point Registers (D0 - D31)
  D0 = 64,
  D1 = 65,
  D2 = 66,
  D3 = 67,
  D4 = 68,
  D5 = 69,
  D6 = 70,
  D7 = 71,
  D8 = 72,
  D9 = 73,
  D10 = 74,
  D11 = 75,
  D12 = 76,
  D13 = 77,
  D14 = 78,
  D15 = 79,
  D16 = 80,
  D17 = 81,
  D18 = 82,
  D19 = 83,
  D20 = 84,
  D21 = 85,
  D22 = 86,
  D23 = 87,
  D24 = 88,
  D25 = 89,
  D26 = 90,
  D27 = 91,
  D28 = 92,
  D29 = 93,
  D30 = 94,
  D31 = 95,

  // --- Kernel & Privileged System Registers (Extended IDs) ---
  TPIDRURW = 256, // User Read/Write Thread ID
  TPIDRURO = 257, // User Read-Only Thread ID
  TPIDRPRW = 258, // Privileged Read/Write Thread ID (Kernel thread pointer)
  CPSR = 259,     // Current Program Status Register
  SPSR = 260,     // Saved Program Status Register (Exception mode)
  APSR = 261,
  IAPSR = 262,
  EAPSR = 263,
  XPSR = 264,
  IPSR = 265,
  EPSR = 266,
  IEPSR = 267,
  MSP = 268,
  PSP = 269,
  PRIMASK = 270,
  BASEPRI = 271,
  BASEPRI_MAX = 272,
  FAULTMASK = 273,
  CONTROL = 274,
  SCTLR = 275,
  ACTLR = 276,
  CPACR = 277,
  TTBR0 = 278,
  TTBR1 = 279,
  TTBCR = 280,
  DACR = 281,
  DFSR = 282,
  IFSR = 283,
  DFAR = 284,
  IFAR = 285,
  VBAR = 286,
  CONTEXTIDR = 287,
  MAIR0 = 288,
  MAIR1 = 289,
  AMAIR0 = 290,
  AMAIR1 = 291,
  MIDR = 292,
  MPIDR = 293,

  CNTFRQ = generic_timer::CNTFRQ,
  CNTPCT = generic_timer::CNTPCT,
  CNTVCT = generic_timer::CNTVCT,
  CNTP_TVAL = generic_timer::CNTP_TVAL,
  CNTP_CTL = generic_timer::CNTP_CTL,
  CNTP_CVAL = generic_timer::CNTP_CVAL,
  CNTV_TVAL = generic_timer::CNTV_TVAL,
  CNTV_CTL = generic_timer::CNTV_CTL,
  CNTV_CVAL = generic_timer::CNTV_CVAL,
  CNTHP_TVAL = generic_timer::CNTHP_TVAL,
  CNTHP_CTL = generic_timer::CNTHP_CTL,
  CNTHP_CVAL = generic_timer::CNTHP_CVAL,
  CNTVOFF = generic_timer::CNTVOFF,
  CNTHCTL = generic_timer::CNTHCTL,
  CNTHV_TVAL = generic_timer::CNTHV_TVAL,
  CNTHV_CTL = generic_timer::CNTHV_CTL,
  CNTHV_CVAL = generic_timer::CNTHV_CVAL,
  CNTHPS_TVAL = generic_timer::CNTHPS_TVAL,
  CNTHPS_CTL = generic_timer::CNTHPS_CTL,
  CNTHPS_CVAL = generic_timer::CNTHPS_CVAL,
  CNTHVS_TVAL = generic_timer::CNTHVS_TVAL,
  CNTHVS_CTL = generic_timer::CNTHVS_CTL,
  CNTHVS_CVAL = generic_timer::CNTHVS_CVAL,
  CNTKCTL = generic_timer::CNTKCTL
};

struct register_traits {
  inline static constexpr microfmt::array gpr_registers{
      register_descriptor{"R0", R0},   register_descriptor{"R1", R1},
      register_descriptor{"R2", R2},   register_descriptor{"R3", R3},
      register_descriptor{"R4", R4},   register_descriptor{"R5", R5},
      register_descriptor{"R6", R6},   register_descriptor{"R7", R7},
      register_descriptor{"R8", R8},   register_descriptor{"R9", R9},
      register_descriptor{"R10", R10}, register_descriptor{"FP", FP},
      register_descriptor{"IP", R12},  register_descriptor{"SP", SP},
      register_descriptor{"LR", LR},   register_descriptor{"PC", PC}};

  [[nodiscard]] static constexpr microfmt::array<register_descriptor, 13>
  address_registers() noexcept {
    return {{{"FP", FP}, {"R0", R0}, {"R1", R1}, {"R2", R2}, {"R3", R3},
             {"R4", R4}, {"R5", R5}, {"R6", R6}, {"R7", R7}, {"R8", R8},
             {"R9", R9}, {"R10", R10}, {"IP", R12}}};
  }

  inline static constexpr microfmt::array system_registers{
      register_descriptor{"TPIDRURW", TPIDRURW},
      register_descriptor{"TPIDRURO", TPIDRURO},
      register_descriptor{"TPIDRPRW", TPIDRPRW},
      register_descriptor{"CPSR", CPSR},
      register_descriptor{"SPSR", SPSR},
      register_descriptor{"APSR", APSR},
      register_descriptor{"IAPSR", IAPSR},
      register_descriptor{"EAPSR", EAPSR},
      register_descriptor{"XPSR", XPSR},
      register_descriptor{"IPSR", IPSR},
      register_descriptor{"EPSR", EPSR},
      register_descriptor{"IEPSR", IEPSR},
      register_descriptor{"MSP", MSP},
      register_descriptor{"PSP", PSP},
      register_descriptor{"PRIMASK", PRIMASK},
      register_descriptor{"BASEPRI", BASEPRI},
      register_descriptor{"BASEPRI_MAX", BASEPRI_MAX},
      register_descriptor{"FAULTMASK", FAULTMASK},
      register_descriptor{"CONTROL", CONTROL},
      register_descriptor{"SCTLR", SCTLR},
      register_descriptor{"ACTLR", ACTLR},
      register_descriptor{"CPACR", CPACR},
      register_descriptor{"TTBR0", TTBR0},
      register_descriptor{"TTBR1", TTBR1},
      register_descriptor{"TTBCR", TTBCR},
      register_descriptor{"DACR", DACR},
      register_descriptor{"DFSR", DFSR},
      register_descriptor{"IFSR", IFSR},
      register_descriptor{"DFAR", DFAR},
      register_descriptor{"IFAR", IFAR},
      register_descriptor{"VBAR", VBAR},
      register_descriptor{"CONTEXTIDR", CONTEXTIDR},
      register_descriptor{"MAIR0", MAIR0},
      register_descriptor{"MAIR1", MAIR1},
      register_descriptor{"AMAIR0", AMAIR0},
      register_descriptor{"AMAIR1", AMAIR1},
      register_descriptor{"MIDR", MIDR},
      register_descriptor{"MPIDR", MPIDR},
      register_descriptor{"CNTFRQ", CNTFRQ},
      register_descriptor{"CNTPCT", CNTPCT},
      register_descriptor{"CNTVCT", CNTVCT},
      register_descriptor{"CNTP_TVAL", CNTP_TVAL},
      register_descriptor{"CNTP_CTL", CNTP_CTL},
      register_descriptor{"CNTP_CVAL", CNTP_CVAL},
      register_descriptor{"CNTV_TVAL", CNTV_TVAL},
      register_descriptor{"CNTV_CTL", CNTV_CTL},
      register_descriptor{"CNTV_CVAL", CNTV_CVAL},
      register_descriptor{"CNTHP_TVAL", CNTHP_TVAL},
      register_descriptor{"CNTHP_CTL", CNTHP_CTL},
      register_descriptor{"CNTHP_CVAL", CNTHP_CVAL},
      register_descriptor{"CNTVOFF", CNTVOFF},
      register_descriptor{"CNTHCTL", CNTHCTL},
      register_descriptor{"CNTHV_TVAL", CNTHV_TVAL},
      register_descriptor{"CNTHV_CTL", CNTHV_CTL},
      register_descriptor{"CNTHV_CVAL", CNTHV_CVAL},
      register_descriptor{"CNTHPS_TVAL", CNTHPS_TVAL},
      register_descriptor{"CNTHPS_CTL", CNTHPS_CTL},
      register_descriptor{"CNTHPS_CVAL", CNTHPS_CVAL},
      register_descriptor{"CNTHVS_TVAL", CNTHVS_TVAL},
      register_descriptor{"CNTHVS_CTL", CNTHVS_CTL},
      register_descriptor{"CNTHVS_CVAL", CNTHVS_CVAL},
      register_descriptor{"CNTKCTL", CNTKCTL}};
};
} // namespace arm32

// ============================================================================
// RISC-V (RV32 / RV64) DWARF Register Numbers
// ============================================================================
namespace riscv {
enum : uint32_t {
  // Integer Registers (X0 - X31) with ABI Name Aliases
  X0 = 0,
  ZERO = 0,
  X1 = 1,
  RA = 1, // Return Address (Link Register)
  X2 = 2,
  SP = 2, // Stack Pointer
  X3 = 3,
  GP = 3, // Global Pointer
  X4 = 4,
  TP = 4, // Thread Pointer
  X5 = 5,
  T0 = 5, // Temporaries
  X6 = 6,
  T1 = 6,
  X7 = 7,
  T2 = 7,
  X8 = 8,
  S0 = 8,
  FP = 8, // Saved Register / Frame Pointer
  X9 = 9,
  S1 = 9,
  X10 = 10,
  A0 = 10, // Function Arguments / Return Values
  X11 = 11,
  A1 = 11,
  X12 = 12,
  A2 = 12,
  X13 = 13,
  A3 = 13,
  X14 = 14,
  A4 = 14,
  X15 = 15,
  A5 = 15,
  X16 = 16,
  A6 = 16,
  X17 = 17,
  A7 = 17,
  X18 = 18,
  S2 = 18, // Saved Registers
  X19 = 19,
  S3 = 19,
  X20 = 20,
  S4 = 20,
  X21 = 21,
  S5 = 21,
  X22 = 22,
  S6 = 22,
  X23 = 23,
  S7 = 23,
  X24 = 24,
  S8 = 24,
  X25 = 25,
  S9 = 25,
  X26 = 26,
  S10 = 26,
  X27 = 27,
  S11 = 27,
  X28 = 28,
  S12 = 28,
  T3 = 28,
  X29 = 29,
  S13 = 29,
  T4 = 29,
  X30 = 30,
  S14 = 30,
  T5 = 30,
  X31 = 31,
  S15 = 31,
  T6 = 31,

  // Floating-Point Registers (F0 - F31)
  F0 = 32,
  F1 = 33,
  F2 = 34,
  F3 = 35,
  F4 = 36,
  F5 = 37,
  F6 = 38,
  F7 = 39,
  F8 = 40,
  F9 = 41,
  F10 = 42,
  F11 = 43,
  F12 = 44,
  F13 = 45,
  F14 = 46,
  F15 = 47,
  F16 = 48,
  F17 = 49,
  F18 = 50,
  F19 = 51,
  F20 = 52,
  F21 = 53,
  F22 = 54,
  F23 = 55,
  F24 = 56,
  F25 = 57,
  F26 = 58,
  F27 = 59,
  F28 = 60,
  F29 = 61,
  F30 = 62,
  F31 = 63,

  // Program Counter
  PC = 65,

  // --- Supervisor & Machine Control Status Registers (CSRs) ---
  SSTATUS = 256,
  SEPC = 257,
  STVAL = 258,
  SATP = 259, // Supervisor Address Translation and Protection (Page Table Base)
  MSTATUS = 260,
  MEPC = 261,
  MTVEC = 262
};

struct register_traits {
  inline static constexpr microfmt::array gpr_registers{
      register_descriptor{"zero", ZERO}, register_descriptor{"ra", RA},
      register_descriptor{"sp", SP},     register_descriptor{"gp", GP},
      register_descriptor{"tp", TP},     register_descriptor{"t0", T0},
      register_descriptor{"t1", T1},     register_descriptor{"t2", T2},
      register_descriptor{"s0", S0},     register_descriptor{"s1", S1},
      register_descriptor{"a0", A0},     register_descriptor{"a1", A1},
      register_descriptor{"a2", A2},     register_descriptor{"a3", A3},
      register_descriptor{"a4", A4},     register_descriptor{"a5", A5},
      register_descriptor{"a6", A6},     register_descriptor{"a7", A7},
      register_descriptor{"s2", S2},     register_descriptor{"s3", S3},
      register_descriptor{"s4", S4},     register_descriptor{"s5", S5},
      register_descriptor{"s6", S6},     register_descriptor{"s7", S7},
      register_descriptor{"s8", S8},     register_descriptor{"s9", S9},
      register_descriptor{"s10", S10},   register_descriptor{"s11", S11},
      register_descriptor{"t3", T3},     register_descriptor{"t4", T4},
      register_descriptor{"t5", T5},     register_descriptor{"t6", T6},
      register_descriptor{"pc", PC}};

  [[nodiscard]] static constexpr microfmt::array<register_descriptor, 29>
  address_registers() noexcept {
    return {{{"s0", FP},  {"gp", GP},  {"tp", TP},   {"a0", A0},
             {"a1", A1},  {"a2", A2},  {"a3", A3},   {"a4", A4},
             {"a5", A5},  {"a6", A6},  {"a7", A7},   {"s1", S1},
             {"s2", S2},  {"s3", S3},  {"s4", S4},   {"s5", S5},
             {"s6", S6},  {"s7", S7},  {"s8", S8},   {"s9", S9},
             {"s10", S10}, {"s11", S11}, {"t0", T0}, {"t1", T1},
             {"t2", T2},  {"t3", T3},  {"t4", T4},   {"t5", T5},
             {"t6", T6}}};
  }

  inline static constexpr microfmt::array system_registers{
      register_descriptor{"sstatus", SSTATUS},
      register_descriptor{"sepc", SEPC},
      register_descriptor{"stval", STVAL},
      register_descriptor{"satp", SATP},
      register_descriptor{"mstatus", MSTATUS},
      register_descriptor{"mepc", MEPC},
      register_descriptor{"mtvec", MTVEC}};
};
} // namespace riscv

} // namespace microfmt::dwarf