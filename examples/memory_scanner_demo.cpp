// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/inspector/memory_scanner.hpp>
#include <microfmt/sinks/stdio.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

struct scanner_classifier_tag {};
struct scanner_symbol_tag {};

struct scanner_classifier_context {
  const microfmt::memory_region_info *regions;
  size_t region_count;
};

MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE

template <> struct microfmt::memory_classifier_traits<scanner_classifier_tag> {
  using context_type = scanner_classifier_context;

  static bool classify_address(const void *opaque_context, uintptr_t address,
                               microfmt::memory_region_info &info) noexcept {
    if (!opaque_context)
      return false;
    const auto &context = *static_cast<const scanner_classifier_context *>(opaque_context);
    for (size_t i = 0; i < context.region_count; ++i) {
      if (context.regions[i].contains(address)) {
        info = context.regions[i];
        return true;
      }
    }
    return false;
  }
};

MICROFMT_END_UNSAFE_BUFFER_USAGE

struct scanner_symbol_context {
  uintptr_t data_address;
  uintptr_t code_address;
};

template <> struct microfmt::symbol_resolver_traits<scanner_symbol_tag> {
  using context_type = scanner_symbol_context;

  static bool resolve(const void *opaque_context, uintptr_t address, microfmt::span<char>,
                      microfmt::raw_resolved_symbol &symbol) noexcept {
    if (!opaque_context)
      return false;
    const auto &context = *static_cast<const scanner_symbol_context *>(opaque_context);
    if (address >= context.data_address && address < context.data_address + 80) {
      symbol.symbol_name = "demo_buffer";
      symbol.symbol_base = context.data_address;
      symbol.is_exact = address == context.data_address;
      return true;
    }
    if (address >= context.code_address && address < context.code_address + 8) {
      symbol.symbol_name = "demo_handler";
      symbol.symbol_base = context.code_address;
      symbol.is_exact = address == context.code_address;
      return true;
    }
    return false;
  }
};

struct demo_registers {
  uintptr_t fp;
  uintptr_t x0;
  uintptr_t x1;
};

bool read_demo_register(const void *opaque_state, microfmt::address_space_ref, uint32_t index, void *output,
                        size_t size) noexcept {
  if (!opaque_state || !output || size != sizeof(uintptr_t))
    return false;
  const auto &registers = *static_cast<const demo_registers *>(opaque_state);
  const uintptr_t *value = nullptr;
  if (index == microfmt::dwarf::aarch64::FP)
    value = &registers.fp;
  else if (index == microfmt::dwarf::aarch64::X0)
    value = &registers.x0;
  else if (index == microfmt::dwarf::aarch64::X1)
    value = &registers.x1;
  if (!value)
    return false;
  MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

  std::memcpy(output, value, size);

  MICROFMT_END_UNSAFE_BUFFER_USAGE;

  return true;
}

int main() {
  std::array<uint8_t, 80> user_data{};
  for (size_t i = 0; i < user_data.size(); ++i) {
    user_data[i] = static_cast<uint8_t>(32 + (i % 95));
  }
  const std::array<uint8_t, 8> kernel_code{0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11};

  const uintptr_t data_address = reinterpret_cast<uintptr_t>(user_data.data());
  const uintptr_t code_address = reinterpret_cast<uintptr_t>(kernel_code.data());
  const microfmt::memory_region_info regions[]{{.start_address = data_address,
                                                .end_address = data_address + user_data.size(),
                                                .type = microfmt::memory_region_type::user_data,
                                                .space_id = 1,
                                                .readable = true,
                                                .writable = true,
                                                .executable = false},
                                               {.start_address = code_address,
                                                .end_address = code_address + kernel_code.size(),
                                                .type = microfmt::memory_region_type::kernel_code,
                                                .space_id = 0,
                                                .readable = true,
                                                .writable = false,
                                                .executable = true}};
  const scanner_classifier_context classifier_context{regions, sizeof(regions) / sizeof(regions[0])};
  const auto classifier = microfmt::memory_classifier_ref::make<scanner_classifier_tag>(classifier_context);
  const auto space = microfmt::address_space_ref{microfmt::local_space_tag{}};
  const scanner_symbol_context symbol_context{data_address, code_address};
  const auto resolver = microfmt::symbol_resolver_ref::make<scanner_symbol_tag>(symbol_context);
  static std::array<char, 64> symbol_scratch{};
  static microfmt::memory_scanner_context scanner_context;
  scanner_context.options.symbol_resolver = resolver;
  scanner_context.symbol_scratch = {symbol_scratch.data(), symbol_scratch.size()};

  demo_registers register_values{.fp = data_address, .x0 = code_address, .x1 = 0};
  std::byte register_scratch[sizeof(uintptr_t)]{};
  microfmt::register_context_ref register_context(&register_values, {&read_demo_register, nullptr}, space,
                                                  register_scratch);
  const uintptr_t explicit_addresses[]{data_address + 8, 0};

  microfmt::println("Register and explicit-address memory scan:");
  microfmt::memory_scanner::scan_and_dump<microfmt::aarch64_abi_traits>(
      space, classifier, register_context, explicit_addresses, scanner_context, microfmt::stdout_sink());
}
