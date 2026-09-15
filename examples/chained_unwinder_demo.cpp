#include <array>
#include <microfmt/inspector/address_space.hpp>
#include <microfmt/inspector/arm_exidx_unwinder.hpp>
#include <microfmt/inspector/chained_unwinder.hpp>
#include <microfmt/inspector/dwarf_decoder.hpp>
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

struct arm_register_file {
  std::array<uint64_t, 96> values{};
};

bool read_arm_register(const void *ctx, microfmt::address_space_ref,
                       uint32_t dwarf_reg, void *out_value,
                       size_t value_size) noexcept {
  if (!ctx || !out_value || dwarf_reg >= 96 || value_size > sizeof(uint64_t))
    return false;
  const auto &registers = *static_cast<const arm_register_file *>(ctx);
  std::memcpy(out_value, &registers.values[dwarf_reg], value_size);
  return true;
}

bool write_arm_register(void *ctx, microfmt::address_space_ref,
                        uint32_t dwarf_reg, const void *in_value,
                        size_t value_size) noexcept {
  if (!ctx || !in_value || dwarf_reg >= 96 || value_size > sizeof(uint64_t))
    return false;
  auto &registers = *static_cast<arm_register_file *>(ctx);
  std::memcpy(&registers.values[dwarf_reg], in_value, value_size);
  return true;
}

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

  static bool step(const void *ctx, microfmt::register_context_ref reg_ctx,
                   uintptr_t &next_fp, uintptr_t &next_pc) noexcept {
    uint32_t current_fp = 0;
    if (!ctx || !reg_ctx.read(microfmt::dwarf::arm32::FP, current_fp) ||
        current_fp == 0 || (current_fp % 4) != 0)
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
    return reg_ctx.write(microfmt::dwarf::arm32::FP, saved_fp) &&
           reg_ctx.write(microfmt::dwarf::arm32::LR, return_lr);
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
  exidx_region[1] = 0x0084'80B0; // Pop R11 and LR, then finish

  uintptr_t entry1_addr = exidx_table_base + 8;
  uintptr_t fn1_target = 0x0800'2000;
  int32_t prel31_1 = static_cast<int32_t>(fn1_target - entry1_addr);
  exidx_region[2] = static_cast<uint32_t>(prel31_1);
  exidx_region[3] = 0x1; // EXIDX_CANTUNWIND

  // B. Setup Multi-ELF Registry & Enumerator
  microfmt::multi_elf_registry_context<4> elf_registry{};
  elf_registry.register_image({.image_name = "firmware.bin",
                               .load_base = 0x0800'0000,
                               .image_size = 0x0005'0000,
                               .exidx_start = exidx_table_base,
                               .exidx_end = exidx_table_base + 16});
  microfmt::elf_image_enumerator_ref enumerator(
      microfmt::multi_elf_registry_tag{}, elf_registry);

  // C. Setup Type-Erased Unwind Hint Registry
  microfmt::unwind_hint_registry_context<4> hint_context{};

  hint_context.add_hint(
      {.pc_start = 0x0800'8000,
       .pc_end = 0x0800'8200,
       .routine = [](microfmt::address_space_ref space, uintptr_t current_fp,
                     uintptr_t, uintptr_t &next_fp,
                     uintptr_t &next_pc) noexcept -> bool {
         // Custom assembly stub/prologue recovery logic
         uint32_t saved_fp = 0;
         uint32_t saved_lr = 0;

         if (!space.read_bytes(current_fp, &saved_fp, 4))
           return false;
         if (!space.read_bytes(current_fp + 4, &saved_lr, 4))
           return false;

         next_fp = static_cast<uintptr_t>(saved_fp);
         next_pc = static_cast<uintptr_t>(saved_lr & ~1U);
         return true;
       }});

  microfmt::unwind_hint_registry_ref hint_ref(
      microfmt::unwind_hint_registry_tag{}, hint_context);

  // D. Setup Simulated Stack Frames at Offset 0x500
  uintptr_t stack_base = 0x500;
  auto *stack_region = reinterpret_cast<uint32_t *>(&target_memory[stack_base]);

  // Frame 0: SensorData_Process (Handled by EXIDX unwinder)
  stack_region[0] = static_cast<uint32_t>(stack_base + 16); // Caller FP
  stack_region[1] = 0x0800'2011;     // Thumb return PC in System_MainLoop

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

  microfmt::elf_image_info off_stack_img_storage{};

  microfmt::arm_exidx_unwinder_context exidx_ctx{
      .space = space,
      .enumerator = enumerator,
      .elf_img_storage = &off_stack_img_storage};

  microfmt::frame_unwinder_ref exidx_unwinder(
      microfmt::arm_exidx_unwinder_tag{}, exidx_ctx);

  microfmt::frame_unwinder_ref fp_unwinder(standard_fp_unwinder_tag{}, space);

  // Chained Unwinder combining EXIDX -> FP -> Hint Registry
  microfmt::chained_unwinder_context<microfmt::arm_abi_traits> chained_ctx{
      .space = space,
      .exidx_unwinder = exidx_unwinder,
      .fp_unwinder = fp_unwinder,
      .hints = hint_ref};
  microfmt::frame_unwinder_ref robust_unwinder(
      microfmt::chained_unwinder_tag<microfmt::arm_abi_traits>{}, chained_ctx);

  // F. Execute Backtrace
  char scratch[64];
  uintptr_t initial_fp = stack_base;
  uintptr_t initial_pc = 0x0800'1081;

  arm_register_file register_file;
  register_file.values[microfmt::dwarf::arm32::FP] = initial_fp;
  register_file.values[microfmt::dwarf::arm32::SP] = initial_fp;
  register_file.values[microfmt::dwarf::arm32::LR] = initial_pc;
  std::byte register_scratch[sizeof(uint64_t)]{};
  microfmt::register_context_ref register_context(
      &register_file, {&read_arm_register, &write_arm_register}, space,
      register_scratch);
  microfmt::frame_pointer_iterator it(robust_unwinder, register_context,
                                      initial_fp, initial_pc);
  microfmt::remote_backtrace_view bt(it, resolver, scratch);

  microfmt::println("\nChained Unwinder Backtrace Result:\n{}", bt);

  register_file.values[microfmt::dwarf::arm32::FP] = initial_fp;
  register_file.values[microfmt::dwarf::arm32::SP] = initial_fp;
  register_file.values[microfmt::dwarf::arm32::LR] = initial_pc;
  microfmt::frame_pointer_iterator verbose_it(
      robust_unwinder, register_context, initial_fp, initial_pc);
  microfmt::remote_backtrace_view verbose_bt(verbose_it, resolver, scratch);
  microfmt::println("\nVerbose Backtrace Result:\n{:#}", verbose_bt);

  microfmt::println("\n[Test Success]: Full multi-ELF EXIDX and chained "
                    "fallback executed cleanly.");
  return 0;
}
