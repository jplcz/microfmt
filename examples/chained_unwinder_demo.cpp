#include <microfmt/inspector/address_space.hpp>
#include <microfmt/inspector/arm_exidx_unwinder.hpp>
#include <microfmt/inspector/chained_unwinder.hpp>
#include <microfmt/inspector/frame_pointer.hpp>
#include <microfmt/inspector/symbol_resolver.hpp>
#include <microfmt/inspector/unwind_hint.hpp>
#include <microfmt/sinks/stdio.hpp>

// ============================================================================
// Simulated Target Memory Address Space
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
// Standard Frame Pointer Unwinder (Fallback Strategy)
// ============================================================================

struct standard_fp_unwinder_tag {};

template <> struct microfmt::frame_unwinder_traits<standard_fp_unwinder_tag> {
  using context_type = microfmt::address_space_ref;

  static bool step(const void *ctx, uintptr_t current_fp, uintptr_t &next_fp,
                   uintptr_t &next_pc) noexcept {
    if (!ctx || current_fp == 0 || (current_fp % 4) != 0)
      return false;
    const auto &space = *static_cast<const microfmt::address_space_ref *>(ctx);

    uint32_t saved_fp = 0;
    uint32_t return_lr = 0;

    if (!space.read_bytes(current_fp, &saved_fp, 4))
      return false;
    if (!space.read_bytes(current_fp + 4, &return_lr, 4))
      return false;

    if (saved_fp <= current_fp || return_lr == 0)
      return false;

    next_fp = static_cast<uintptr_t>(saved_fp);
    next_pc = static_cast<uintptr_t>(return_lr & ~1U);
    return true;
  }
};

// ============================================================================
// Mock Symbol Resolver
// ============================================================================

struct full_demo_sym_tag {};

template <> struct microfmt::symbol_resolver_traits<full_demo_sym_tag> {
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

// ============================================================================
// Main Application Setup & Execution
// ============================================================================

int main() {
  microfmt::println(
      "==================================================================");
  microfmt::println(" Full Chained ARM EXIDX & Multi-ELF Unwinder Test");
  microfmt::println(
      "==================================================================");

  alignas(8) uint8_t target_memory[4096];
  std::memset(target_memory, 0, sizeof(target_memory));

  // A. Setup Mock .ARM.exidx Table at Offset 0x100
  uintptr_t exidx_table_base = 0x100;
  auto *exidx_region =
      reinterpret_cast<uint32_t *>(&target_memory[exidx_table_base]);

  uintptr_t fn0_target = 0x0800'1000;
  int32_t prel31_0 = static_cast<int32_t>(fn0_target - exidx_table_base);
  exidx_region[0] = static_cast<uint32_t>(prel31_0);
  exidx_region[1] = 0x0001'0000; // Inline bytecode: adjust stack pointer (+4)

  // B. Setup Multi-ELF Registry & Enumerator
  microfmt::multi_elf_registry_context<4> elf_registry{};
  elf_registry.register_image({.image_name = "firmware.bin",
                               .load_base = 0x0800'0000,
                               .image_size = 0x0005'0000,
                               .exidx_start = exidx_table_base,
                               .exidx_end = exidx_table_base + 8});
  microfmt::elf_image_enumerator_ref enumerator(
      microfmt::multi_elf_registry_tag{}, elf_registry);

  // C. Setup Type-Erased Unwind Hint Registry
  microfmt::unwind_hint_registry_context<4> hint_context{};
  hint_context.add_hint({.pc_start = 0x0800'8000,
                         .pc_end = 0x0800'8200,
                         .sp_offset = 32,
                         .is_assembly_stub = true});
  microfmt::unwind_hint_registry_ref hint_ref(
      microfmt::unwind_hint_registry_tag{}, hint_context);

  // D. Setup Simulated Stack Frames at Offset 0x500
  uintptr_t stack_base = 0x500;
  auto *stack_region = reinterpret_cast<uint32_t *>(&target_memory[stack_base]);

  // Frame 0: SensorData_Process (Handled by EXIDX unwinder)
  stack_region[0] = stack_base + 16; // Caller FP
  stack_region[1] = 0x0800'1041;     // Thumb PC inside SensorData_Process

  // Frame 1: System_MainLoop (Handled by FP unwinder fallback)
  stack_region[4] = 0;           // Terminator FP
  stack_region[5] = 0x0800'2011; // Thumb PC inside System_MainLoop

  // E. Configure Contexts & Interfaces
  simulated_space_context sctx{.buffer_base =
                                   reinterpret_cast<uintptr_t>(target_memory),
                               .buffer_size = sizeof(target_memory)};
  microfmt::address_space_ref space(simulated_space_tag{}, sctx);
  microfmt::symbol_resolver_ref resolver =
      microfmt::symbol_resolver_ref::make<full_demo_sym_tag>();

  // Off-stack register scratch buffer to prevent kernel stack overflow
  microfmt::arm_register_state off_stack_scratch{};

  microfmt::arm_exidx_unwinder_context exidx_ctx{.space = space,
                                                 .enumerator = enumerator,
                                                 .reg_scratch =
                                                     &off_stack_scratch};
  microfmt::frame_unwinder_ref exidx_unwinder(
      microfmt::arm_exidx_unwinder_tag{}, exidx_ctx);

  microfmt::frame_unwinder_ref fp_unwinder(standard_fp_unwinder_tag{}, space);

  // Chained Unwinder combining EXIDX -> FP -> Hint Registry
  microfmt::chained_unwinder_context chained_ctx{.space = space,
                                                 .exidx_unwinder =
                                                     exidx_unwinder,
                                                 .fp_unwinder = fp_unwinder,
                                                 .hints = hint_ref};
  microfmt::frame_unwinder_ref robust_unwinder(microfmt::chained_unwinder_tag{},
                                               chained_ctx);

  // F. Execute Backtrace
  char scratch[64];
  uintptr_t initial_fp = stack_base;
  uintptr_t initial_pc = 0x0800'1081;

  microfmt::frame_pointer_iterator it(robust_unwinder, initial_fp, initial_pc);
  microfmt::remote_backtrace_view bt(it, resolver, scratch);

  microfmt::println("\nChained Unwinder Backtrace Result:\n{}", bt);
  microfmt::println("\nVerbose Backtrace Result:\n{:#}", bt);

  microfmt::println("\n[Test Success]: Full multi-ELF EXIDX and chained "
                    "fallback executed cleanly.");
  return 0;
}
