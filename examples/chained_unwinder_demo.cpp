// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <microfmt/inspector/address_space.hpp>
#include <microfmt/inspector/arm_exidx_unwinder.hpp>
#include <microfmt/inspector/chained_unwinder.hpp>
#include <microfmt/inspector/dwarf_decoder.hpp>
#include <microfmt/inspector/frame_pointer.hpp>
#include <microfmt/inspector/symbol_resolver.hpp>
#include <microfmt/inspector/unwind_hint.hpp>
#include <microfmt/sinks/stdio.hpp>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

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

  static bool read_bytes(microfmt::value_ref<const context_type> context,
                         uintptr_t addr, void *dest,
                         size_t size) noexcept {
    if (addr == 0 || !dest)
      return false;
    if (addr > context->buffer_size || size > context->buffer_size - addr)
      return false;
    std::memcpy(
        dest, reinterpret_cast<const void *>(context->buffer_base + addr),
        size);
    return true;
  }

  static bool read_string(microfmt::value_ref<const context_type> context,
                          uintptr_t addr, char *dest,
                          size_t max_len, size_t &out_len,
                          bool &null_term) noexcept {
    if (addr == 0 || !dest || max_len == 0)
      return false;
    if (addr >= context->buffer_size)
      return false;
    const auto *src =
        reinterpret_cast<const char *>(context->buffer_base + addr);
    size_t avail = context->buffer_size - addr;
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

struct standard_fp_unwinder_tag {};

template <> struct microfmt::frame_unwinder_traits<standard_fp_unwinder_tag> {
  using context_type = microfmt::address_space_ref;

  static bool step(microfmt::value_ref<const context_type> context,
                   microfmt::register_context_ref reg_ctx,
                   uintptr_t /*current_pc*/, uintptr_t &next_fp,
                   uintptr_t &next_pc) noexcept {
    uint32_t current_fp = 0;
    if (!reg_ctx.read(microfmt::dwarf::arm32::fp, current_fp) ||
        current_fp == 0 || (current_fp % 4) != 0)
      return false;
    uint32_t saved_fp = 0;
    uint32_t return_lr = 0;
    if (!context->read_bytes(current_fp, &saved_fp, 4) ||
        !context->read_bytes(current_fp + 4, &return_lr, 4) ||
        saved_fp <= current_fp || return_lr == 0)
      return false;
    next_fp = static_cast<uintptr_t>(saved_fp);
    next_pc = static_cast<uintptr_t>(return_lr & ~1U);
    return reg_ctx.write(microfmt::dwarf::arm32::fp, saved_fp) &&
           reg_ctx.write(microfmt::dwarf::arm32::lr, return_lr);
  }
};

struct full_demo_sym_tag {};

template <> struct microfmt::symbol_resolver_traits<full_demo_sym_tag> {
  using context_type = void;
  static bool resolve(uintptr_t addr, microfmt::span<char>,
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
  microfmt::println("==================================================================");
  microfmt::println(" Full Chained ARM EXIDX & Multi-ELF Unwinder Test");
  microfmt::println("==================================================================");

  alignas(8) uint8_t target_memory[4096];
  std::memset(target_memory, 0, sizeof(target_memory));

  uintptr_t exidx_table_base = 0x100;
  auto *exidx_region = static_cast<uint32_t *>(
      static_cast<void *>(&target_memory[exidx_table_base]));
  uintptr_t fn0_target = 0x0800'1000;
  int32_t prel31_0 = static_cast<int32_t>(fn0_target - exidx_table_base);
  exidx_region[0] = static_cast<uint32_t>(prel31_0);
  // Compact model (ARM EHABI #6.3): bit 31 set, personality index 0, inline
  // opcode bytes 0x84 0x80 0xB0 (pop R11/LR, then finish).
  exidx_region[1] = 0x8084'80B0;
  uintptr_t entry1_addr = exidx_table_base + 8;
  uintptr_t fn1_target = 0x0800'2000;
  int32_t prel31_1 = static_cast<int32_t>(fn1_target - entry1_addr);
  exidx_region[2] = static_cast<uint32_t>(prel31_1);
  exidx_region[3] = 0x1;

  microfmt::multi_elf_registry_context<4> elf_registry{};
  elf_registry.register_image({.image_name = "firmware.bin",
                               .load_base = 0x0800'0000,
                               .image_size = 0x0005'0000,
                               .exidx_start = exidx_table_base,
                               .exidx_end = exidx_table_base + 16});
  microfmt::elf_image_enumerator_ref enumerator(
      microfmt::multi_elf_registry_tag<4>{}, elf_registry);

  microfmt::unwind_hint_registry_context<4> hint_context{};
  hint_context.add_hint({
      .pc_start = 0x0800'8000,
      .pc_end = 0x0800'8200,
      .routine = [](microfmt::address_space_ref space,
                    microfmt::register_context_ref reg_ctx,
                    uintptr_t &next_fp, uintptr_t &next_pc) noexcept -> bool {
        uint32_t context_address = 0;
        uint32_t saved_fp = 0;
        uint32_t saved_lr = 0;
        if (!reg_ctx.read(microfmt::dwarf::arm32::r0, context_address) ||
            !space.read_bytes(context_address, &saved_fp, 4) ||
            !space.read_bytes(context_address + 4, &saved_lr, 4))
          return false;
        if (!reg_ctx.write(microfmt::dwarf::arm32::fp, saved_fp) ||
            !reg_ctx.write(microfmt::dwarf::arm32::lr, saved_lr))
          return false;
        next_fp = static_cast<uintptr_t>(saved_fp);
        next_pc = static_cast<uintptr_t>(saved_lr & ~1U);
        return next_fp != 0 && next_pc != 0;
      }});
  microfmt::unwind_hint_registry_ref hint_ref(
      microfmt::fixed_unwind_hint_registry_tag<4>{}, hint_context);

  uintptr_t stack_base = 0x500;
  auto *stack_region = static_cast<uint32_t *>(
      static_cast<void *>(&target_memory[stack_base]));
  stack_region[0] = static_cast<uint32_t>(stack_base + 16);
  stack_region[1] = 0x0800'2011;
  stack_region[4] = 0;
  stack_region[5] = 0x0800'2011;

  simulated_space_context sctx{.buffer_base = reinterpret_cast<uintptr_t>(target_memory),
                               .buffer_size = sizeof(target_memory)};
  microfmt::address_space_ref space(simulated_space_tag{}, sctx);
  microfmt::symbol_resolver_ref resolver =
      microfmt::symbol_resolver_ref::make<full_demo_sym_tag>();
  microfmt::elf_image_info off_stack_img_storage{};
  microfmt::arm_exidx_unwinder_context exidx_ctx{
      .space = space, .enumerator = enumerator,
      .elf_img_storage = &off_stack_img_storage};
  microfmt::frame_unwinder_ref exidx_unwinder(
      microfmt::arm_exidx_unwinder_tag<>{}, exidx_ctx);
  microfmt::frame_unwinder_ref fp_unwinder(standard_fp_unwinder_tag{}, space);
  microfmt::chained_unwinder_context<microfmt::arm_abi_traits> chained_ctx{
      .space = space, .exidx_unwinder = exidx_unwinder,
      .fp_unwinder = fp_unwinder, .hints = hint_ref};
  microfmt::frame_unwinder_ref robust_unwinder(
      microfmt::chained_unwinder_tag<microfmt::arm_abi_traits>{}, chained_ctx);

  char scratch[64];
  microfmt::symbol_resolution_context symbol_context{scratch};
  uintptr_t initial_fp = stack_base;
  uintptr_t initial_pc = 0x0800'1081;
  arm_register_file register_file;
  register_file.values[microfmt::dwarf::arm32::fp] = initial_fp;
  register_file.values[microfmt::dwarf::arm32::sp] = initial_fp;
  std::byte register_scratch[sizeof(uint64_t)]{};
  auto register_context =
      microfmt::make_register_context_ref<read_arm_register,
                                          write_arm_register>(
          register_file, space, register_scratch);
  microfmt::frame_pointer_iterator it(robust_unwinder, register_context,
                                      initial_fp, initial_pc);
  microfmt::remote_backtrace_view bt(it, resolver, symbol_context);
  microfmt::println("\nChained Unwinder Backtrace Result:\n{}", bt);

  register_file.values[microfmt::dwarf::arm32::fp] = initial_fp;
  register_file.values[microfmt::dwarf::arm32::sp] = initial_fp;
  microfmt::frame_pointer_iterator verbose_it(robust_unwinder, register_context,
                                              initial_fp, initial_pc);
  microfmt::remote_backtrace_view verbose_bt(verbose_it, resolver,
                                             symbol_context);
  microfmt::println("\nVerbose Backtrace Result:\n{:#}", verbose_bt);
  microfmt::println("\n[Test Success]: Full multi-ELF EXIDX and chained fallback executed cleanly.");
  return 0;
}

RELOCO_END_UNSAFE_BUFFER_USAGE
