// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <cstring>
#include <iostream>
#include <microfmt/inspector/address_space.hpp>
#include <microfmt/inspector/hybrid_unwinder.hpp>
#include <microfmt/inspector/symbol_resolver.hpp>
#include <microfmt/sinks/stdio.hpp>

// ============================================================================
// Mock Kernel & User Symbol Table
// ============================================================================

struct arch_x86_64_tag {};

struct arch_unwinder_context {
  microfmt::address_space_ref space;
  uintptr_t stack_floor{0}; // Lower bound (e.g. current SP)
  uintptr_t stack_ceil{0};  // Upper bound (e.g. stack top/base)
};

struct arch_register_state {
  uint64_t fp{0};
  uint64_t pc{0};
};

bool read_arch_register(const void *ctx, microfmt::address_space_ref,
                        uint32_t dwarf_reg, void *out_value,
                        size_t value_size) noexcept {
  if (!ctx || !out_value || value_size != sizeof(uint64_t))
    return false;
  const auto &state = *static_cast<const arch_register_state *>(ctx);
  const uint64_t *value = nullptr;
  if (dwarf_reg == microfmt::dwarf::x86_64::FP)
    value = &state.fp;
  else if (dwarf_reg == microfmt::dwarf::x86_64::PC)
    value = &state.pc;
  if (!value)
    return false;
  std::memcpy(out_value, value, value_size);
  return true;
}

bool write_arch_register(void *ctx, microfmt::address_space_ref,
                         uint32_t dwarf_reg, const void *in_value,
                         size_t value_size) noexcept {
  if (!ctx || !in_value || value_size != sizeof(uint64_t))
    return false;
  auto &state = *static_cast<arch_register_state *>(ctx);
  uint64_t *value = nullptr;
  if (dwarf_reg == microfmt::dwarf::x86_64::FP)
    value = &state.fp;
  else if (dwarf_reg == microfmt::dwarf::x86_64::PC)
    value = &state.pc;
  if (!value)
    return false;
  std::memcpy(value, in_value, value_size);
  return true;
}

struct trap_x86_64_tag {};

struct arch_trap_context {
  microfmt::address_space_ref space;
};

template <> struct microfmt::frame_unwinder_traits<arch_x86_64_tag> {
  using context_type = arch_unwinder_context;

  static bool step(const void *ctx, microfmt::register_context_ref reg_ctx,
                   uintptr_t &next_fp, uintptr_t &next_pc) noexcept {
    uint64_t raw_fp = 0;
    if (!ctx || !reg_ctx.read(microfmt::dwarf::x86_64::FP, raw_fp))
      return false;
    uintptr_t current_fp = static_cast<uintptr_t>(raw_fp);
    if (current_fp == 0 || (current_fp % 8) != 0)
      return false;
    const auto &cfg = *static_cast<const arch_unwinder_context *>(ctx);

    if (cfg.stack_floor != 0 && current_fp < cfg.stack_floor)
      return false;
    if (cfg.stack_ceil != 0 && (current_fp + 16) > cfg.stack_ceil)
      return false;

    uint64_t saved_rbp = 0;
    uint64_t return_rip = 0;

    if (!cfg.space.read_bytes(current_fp, &saved_rbp, 8))
      return false;
    if (!cfg.space.read_bytes(current_fp + 8, &return_rip, 8))
      return false;

    // Stack grows downwards -> Caller FP must be strictly greater
    if (saved_rbp <= current_fp || return_rip == 0)
      return false;

    next_fp = static_cast<uintptr_t>(saved_rbp);
    next_pc = static_cast<uintptr_t>(return_rip);
    return reg_ctx.write(microfmt::dwarf::x86_64::FP, saved_rbp) &&
           reg_ctx.write(microfmt::dwarf::x86_64::PC, return_rip);
  }
};

struct symbol_tag {};
template <> struct microfmt::symbol_resolver_traits<symbol_tag> {
  using context_type = void;
  static bool resolve(const void *, uintptr_t addr, microfmt::span<char>,
                      microfmt::raw_resolved_symbol &out_raw) noexcept {
    // Kernel symbols
    if (addr >= 0xffff'8000'0010'0000ULL && addr < 0xffff'8000'0010'0100ULL) {
      out_raw.image_name = "vmlinux";
      out_raw.symbol_name = "page_fault_handler";
      out_raw.symbol_base = 0xffff'8000'0010'0000ULL;
      return true;
    }
    if (addr >= 0xffff'8000'0010'0200ULL && addr < 0xffff'8000'0010'0300ULL) {
      out_raw.image_name = "vmlinux";
      out_raw.symbol_name = "asm_exc_page_fault"; // Trampoline site
      out_raw.symbol_base = 0xffff'8000'0010'0200ULL;
      return true;
    }

    // User-space symbols
    if (addr >= 0x0000'0000'0040'1000ULL && addr < 0x0000'0000'0040'2000ULL) {
      out_raw.image_name = "user_app";
      out_raw.symbol_name = "_ZN4core8database5queryEPKc";
      out_raw.symbol_base = 0x0000'0000'0040'1000ULL;
      return true;
    }
    if (addr >= 0x0000'0000'0040'2000ULL && addr < 0x0000'0000'0040'3000ULL) {
      out_raw.image_name = "user_app";
      out_raw.symbol_name = "main";
      out_raw.symbol_base = 0x0000'0000'0040'2000ULL;
      return true;
    }
    return false;
  }
};

template <> struct microfmt::exception_frame_traits<trap_x86_64_tag> {
  using context_type = arch_trap_context;

  // ==========================================================================
  //  Decode x86-64 pt_regs memory layout into normalized trap_context
  // ==========================================================================
  static bool decode(const void *ctx, uintptr_t trap_frame_addr,
                     trap_context &out_trap) noexcept {
    if (!ctx || trap_frame_addr == 0)
      return false;
    const auto &tctx = *static_cast<const arch_trap_context *>(ctx);

    // Ensure space reference is valid
    if (!tctx.space)
      return false;

    uint64_t rip = 0;
    uint64_t rsp = 0;
    uint64_t rbp = 0;
    uint64_t cs = 0;
    uint64_t orig_rax = 0;

    // Standard Linux x86_64 struct pt_regs byte offsets:
    //   +0x20: rbp (saved frame pointer)
    //   +0x70: orig_rax (vector number or syscall code)
    //   +0x78: rip (instruction pointer)
    //   +0x80: cs  (code segment selector)
    //   +0x90: rsp (stack pointer)
    constexpr size_t kRbpOffset = 0x20;
    constexpr size_t kOrigRaxOffset = 0x70;
    constexpr size_t kRipOffset = 0x78;
    constexpr size_t kCsOffset = 0x80;
    constexpr size_t kRspOffset = 0x90;

    // Safely read registers across the address space boundary
    if (!tctx.space.read_bytes(trap_frame_addr + kRipOffset, &rip, sizeof(rip)))
      return false;
    if (!tctx.space.read_bytes(trap_frame_addr + kCsOffset, &cs, sizeof(cs)))
      return false;
    if (!tctx.space.read_bytes(trap_frame_addr + kRspOffset, &rsp, sizeof(rsp)))
      return false;
    if (!tctx.space.read_bytes(trap_frame_addr + kRbpOffset, &rbp, sizeof(rbp)))
      return false;

    // Non-fatal optional fields
    std::ignore = tctx.space.read_bytes(trap_frame_addr + kOrigRaxOffset,
                                        &orig_rax, sizeof(orig_rax));

    // Populate the normalized target-agnostic trap snapshot
    out_trap.pc = static_cast<uintptr_t>(rip);
    out_trap.sp = static_cast<uintptr_t>(rsp);
    out_trap.fp = static_cast<uintptr_t>(rbp);
    out_trap.lr = 0; // x86-64 uses hardware stack for return addresses instead
                     // of a link register (LR)
    out_trap.vector_or_reason = orig_rax;

    // In x86 long mode, lowest 2 bits of CS selector indicate privilege ring (3
    // = user mode, 0 = kernel mode)
    out_trap.is_user_mode = ((cs & 3) != 0);

    return true;
  }

  // ==========================================================================
  // Optional: Chain to nested/outer trap frames if present on stack
  // ==========================================================================
  static bool next_trap_frame(const void *, uintptr_t, uintptr_t &) noexcept {
    // Returning false indicates a single primary hardware frame (no nested IRQ
    // stack link)
    return false;
  }

  // ==========================================================================
  // Human-readable descriptions for x86-64 CPU exception vectors
  // ==========================================================================
  static std::string_view describe_reason(const void *,
                                          uint64_t vector_or_reason) noexcept {
    switch (vector_or_reason) {
    case 0x00:
      return "Divide Error (#DE)";
    case 0x01:
      return "Debug (#DB)";
    case 0x03:
      return "Breakpoint (#BP)";
    case 0x04:
      return "Overflow (#OF)";
    case 0x05:
      return "BOUND Range Exceeded (#BR)";
    case 0x06:
      return "Invalid Opcode (#UD)";
    case 0x07:
      return "Device Not Available (#NM)";
    case 0x08:
      return "Double Fault (#DF)";
    case 0x0A:
      return "Invalid TSS (#TS)";
    case 0x0B:
      return "Segment Not Present (#NP)";
    case 0x0C:
      return "Stack-Segment Fault (#SS)";
    case 0x0D:
      return "General Protection Fault (#GP)";
    case 0x0E:
      return "Page Fault (#PF)";
    case 0x10:
      return "x87 FPU Floating-Point Error (#MF)";
    case 0x11:
      return "Alignment Check (#AC)";
    case 0x12:
      return "Machine Check (#MC)";
    case 0x13:
      return "SIMD Floating-Point Exception (#XM)";
    case 0x14:
      return "Virtualization Exception (#VE)";
    case 0x1E:
      return "Security Exception (#SX)";
    default:
      return {};
    }
  }
};

int main() {
  alignas(16) uint8_t simulated_ram[2048];
  std::memset(simulated_ram, 0, sizeof(simulated_ram));

  // --- Stack Layout ---
  // Kernel Stack: [0x1000 .. 0x1300]
  // User Stack:   [0x7ffe0000 .. 0x7ffe0200]

  // User Stack Frames
  auto *user_f1 =
      reinterpret_cast<uintptr_t *>(&simulated_ram[0x600]); // main frame
  user_f1[0] = 0;                                           // Terminating frame
  user_f1[1] = 0;

  auto *user_f0 =
      reinterpret_cast<uintptr_t *>(&simulated_ram[0x500]); // query() frame
  user_f0[0] = reinterpret_cast<uintptr_t>(user_f1);
  user_f0[1] = 0x0000'0000'0040'2030ULL; // Return into main+0x30

  // Hardware pt_regs dumped on kernel entry (at simulated offset 0x200)
  auto *pt_regs = &simulated_ram[0x200];
  *reinterpret_cast<uint64_t *>(&pt_regs[0x70]) = 0x0E; // Vector 14: Page Fault
  *reinterpret_cast<uint64_t *>(&pt_regs[0x78]) =
      0x0000'0000'0040'1044ULL; // Interrupted PC: query+0x44
  *reinterpret_cast<uint64_t *>(&pt_regs[0x80]) = 0x33; // User CS
  *reinterpret_cast<uint64_t *>(&pt_regs[0x90]) =
      reinterpret_cast<uintptr_t>(user_f0); // User RSP
  *reinterpret_cast<uint64_t *>(&pt_regs[0x20]) =
      reinterpret_cast<uintptr_t>(user_f0); // User RBP

  // Kernel Stack Frames
  auto *k_f1 = reinterpret_cast<uintptr_t *>(
      &simulated_ram[0x100]);         // asm_exc_page_fault
  k_f1[0] = 0;                        // End of kernel FP chain
  k_f1[1] = 0xffff'8000'0010'0210ULL; // PC inside asm_exc_page_fault

  auto *k_f0 = reinterpret_cast<uintptr_t *>(
      &simulated_ram[0x080]); // page_fault_handler
  k_f0[0] = reinterpret_cast<uintptr_t>(k_f1);
  k_f0[1] = 0xffff'8000'0010'0208ULL; // Return into asm_exc_page_fault+0x8

  // Transport & Traits
  microfmt::address_space_ref space(microfmt::local_space_tag{});
  microfmt::symbol_resolver_ref resolver =
      microfmt::symbol_resolver_ref::make<symbol_tag>();

  // Architecture FP unwinder
  arch_unwinder_context uctx{.space = space};
  microfmt::frame_unwinder_ref fp_unwinder(arch_x86_64_tag{}, uctx);

  // Exception frame decoder
  arch_trap_context tctx{.space = space};
  microfmt::exception_frame_ref trap_decoder(trap_x86_64_tag{}, tctx);

  arch_register_state register_state{};

  // Matcher: Detects if PC is in 'asm_exc_page_fault', pointing to the saved
  // pt_regs
  auto matcher_fn = [&](uintptr_t /*fp*/, uintptr_t pc,
                        uintptr_t &out_addr) noexcept -> bool {
    if (pc >= 0xffff'8000'0010'0200ULL && pc < 0xffff'8000'0010'0300ULL) {
      out_addr = reinterpret_cast<uintptr_t>(pt_regs);
      register_state.fp = reinterpret_cast<uintptr_t>(user_f0);
      register_state.pc = 0x0000'0000'0040'1044ULL;
      return true;
    }
    return false;
  };
  microfmt::exception_matcher_ref matcher(matcher_fn);

  // Initialize Hybrid Unwinder
  uintptr_t initial_fp = reinterpret_cast<uintptr_t>(k_f0);
  uintptr_t initial_pc =
      0xffff'8000'0010'0020ULL; // PC inside page_fault_handler

  register_state = {initial_fp, initial_pc};
  std::byte register_scratch[sizeof(uint64_t)]{};
  microfmt::register_context_ref register_context(
      &register_state, {&read_arch_register, &write_arch_register}, space,
      register_scratch);

  microfmt::hybrid_stack_unwinder unwinder(
      fp_unwinder, trap_decoder, matcher, register_context, initial_fp,
      initial_pc);

  char scratch[64];
  microfmt::hybrid_backtrace_view bt(unwinder, resolver, scratch);

  microfmt::println(
      "==================================================================");
  microfmt::println(" Unified Hybrid Stack Unwinder Demo");
  microfmt::println(
      "==================================================================");
  microfmt::println("{}", bt);

  return 0;
}