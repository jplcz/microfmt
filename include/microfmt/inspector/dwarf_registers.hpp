// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace microfmt::dwarf {

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
  VBAR_EL2 = 267   // Vector Base Address Register EL2
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
  SPSR = 260      // Saved Program Status Register (Exception mode)
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
  X29 = 29,
  S13 = 29,
  X30 = 30,
  S14 = 30,
  X31 = 31,
  S15 = 31,

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
} // namespace riscv

} // namespace microfmt::dwarf