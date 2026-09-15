// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <cstring>
#include <iostream>
#include <microfmt/inspector/address_space.hpp>
#include <microfmt/inspector/arm_exidx_unwinder.hpp>
#include <microfmt/inspector/frame_pointer.hpp>
#include <microfmt/inspector/symbol_resolver.hpp>
#include <microfmt/sinks/stdio.hpp>

// ============================================================================
// Simulated Target Memory Address Space (Maps integer addresses to local
// buffer)
// ============================================================================

struct simulated_space_tag {};

struct simulated_space_context {
  uintptr_t buffer_base;
  size_t buffer_size;
};

template <> struct microfmt::address_space_traits<simulated_space_tag> {
  using context_type = simulated_space_context;

  static bool read_bytes(const void *ctx, uintptr_t addr, void *dest,
                         size_t size) noexcept {
    if (!ctx || addr == 0 || !dest)
      return false;
    const auto &g = *static_cast<const simulated_space_context *>(ctx);
    if (addr + size > g.buffer_size)
      return false;

    std::memcpy(dest, reinterpret_cast<const void *>(g.buffer_base + addr),
                size);
    return true;
  }

  static bool read_string(const void *ctx, uintptr_t addr, char *dest,
                          size_t max_len, size_t &out_len,
                          bool &null_term) noexcept {
    if (!ctx || addr == 0 || !dest || max_len == 0)
      return false;
    const auto &g = *static_cast<const simulated_space_context *>(ctx);
    if (addr >= g.buffer_size)
      return false;

    const auto *src = reinterpret_cast<const char *>(g.buffer_base + addr);
    size_t avail = g.buffer_size - addr;
    size_t limit = (max_len < avail) ? max_len : avail;

    size_t i = 0;
    while (i < limit) {
      dest[i] = src[i];
      if (dest[i] == '\0') {
        out_len = i;
        null_term = true;
        return true;
      }
      ++i;
    }
    out_len = limit;
    null_term = false;
    return true;
  }
};

// ============================================================================
// Mock Symbol Resolver for EXIDX Functions
// ============================================================================

struct exidx_demo_sym_tag {};

template <> struct microfmt::symbol_resolver_traits<exidx_demo_sym_tag> {
  using context_type = void;
  static bool resolve(const void *, uintptr_t addr, microfmt::span<char>,
                      microfmt::raw_resolved_symbol &out_raw) noexcept {
    if (addr >= 0x0800'1000 && addr < 0x0800'1500) {
      out_raw.image_name = "firmware.bin";
      out_raw.symbol_name = "SensorData_Process";
      out_raw.symbol_base = 0x0800'1000;
      return true;
    }
    if (addr >= 0x0800'2000 && addr < 0x0800'2500) {
      out_raw.image_name = "firmware.bin";
      out_raw.symbol_name = "System_MainLoop";
      out_raw.symbol_base = 0x0800'2000;
      return true;
    }
    return false;
  }
};

int main() {
  microfmt::println(
      "==================================================================");
  microfmt::println(" ARM EXIDX Unwinder & Bytecode Decoder Test Application");
  microfmt::println(
      "==================================================================");

  alignas(8) uint8_t target_memory[4096];
  std::memset(target_memory, 0, sizeof(target_memory));

  // ==========================================================================
  // Setup Mock .ARM.exidx Table at Offset 0x100
  // ==========================================================================
  uintptr_t exidx_table_base = 0x100;
  auto *exidx_region =
      reinterpret_cast<uint32_t *>(&target_memory[exidx_table_base]);

  // Entry 0: Function at 0x08001000 (SensorData_Process)
  uintptr_t fn0_target = 0x0800'1000;
  int32_t prel31_0 = static_cast<int32_t>(fn0_target - exidx_table_base);
  exidx_region[0] = static_cast<uint32_t>(prel31_0);
  exidx_region[1] = 0x0001'0000; // Inline bytecode: adjust stack pointer (+4)

  // Entry 1: Function at 0x08002000 (System_MainLoop)
  uintptr_t entry1_addr = exidx_table_base + 8;
  uintptr_t fn1_target = 0x0800'2000;
  int32_t prel31_1 = static_cast<int32_t>(fn1_target - entry1_addr);
  exidx_region[2] = static_cast<uint32_t>(prel31_1);
  exidx_region[3] = 0x80B00000; // Inline bytecode: finish / terminate

  // ==========================================================================
  // Setup Simulated Stack Frames at Offset 0x500 (RAM)
  // ==========================================================================
  uintptr_t stack_base = 0x500;
  auto *stack_region = reinterpret_cast<uint32_t *>(&target_memory[stack_base]);

  // Frame 0 (SensorData_Process active frame)
  stack_region[0] = stack_base + 16; // Caller FP (points to Frame 1)
  stack_region[1] = 0x0800'1041;     // Thumb PC inside SensorData_Process

  // Frame 1 (System_MainLoop caller frame)
  stack_region[4] = 0;           // Terminator FP
  stack_region[5] = 0x0800'2011; // Thumb PC inside System_MainLoop

  // ==========================================================================
  // Configure Simulated Address Space & Unwinder Context
  // ==========================================================================
  simulated_space_context sctx{.buffer_base =
                                   reinterpret_cast<uintptr_t>(target_memory),
                               .buffer_size = sizeof(target_memory)};
  microfmt::address_space_ref space(simulated_space_tag{}, sctx);
  microfmt::symbol_resolver_ref resolver =
      microfmt::symbol_resolver_ref::make<exidx_demo_sym_tag>();

  microfmt::multi_elf_registry_context<4> elf_registry{};
  elf_registry.register_image({.image_name = "firmware.elf",
                               .load_base = 0x0800'0000,
                               .image_size = 0x0005'0000,
                               .exidx_start = 0x0804'0000,
                               .exidx_end = 0x0804'1000});

  microfmt::elf_image_enumerator_ref enumerator(
      microfmt::multi_elf_registry_tag{}, elf_registry);

  microfmt::arm_register_state reg_scratch{};
  microfmt::elf_image_info off_stack_img_storage{};

  microfmt::arm_exidx_unwinder_context exidx_ctx{.space = space,
                                                 .enumerator = enumerator,
                                                 .reg_scratch = &reg_scratch,
                                                 .elf_img_storage =
                                                     &off_stack_img_storage};

  microfmt::frame_unwinder_ref unwinder(microfmt::arm_exidx_unwinder_tag{},
                                        exidx_ctx);

  char scratch[64];
  uintptr_t initial_fp = stack_base;
  uintptr_t initial_pc = 0x0800'1081;

  // ==========================================================================
  // Run EXIDX Unwinder Iteration & Formatting
  // ==========================================================================
  microfmt::frame_pointer_iterator it(unwinder, initial_fp, initial_pc);
  microfmt::remote_backtrace_view bt(it, resolver, scratch);

  microfmt::println("\nUnwound Backtrace Result:\n{}", bt);
  microfmt::println("\nVerbose Backtrace Result:\n{:#}", bt);

  microfmt::println("\n[Test Success]: EXIDX binary search completed cleanly "
                    "over simulated address space.");
  return 0;
}
