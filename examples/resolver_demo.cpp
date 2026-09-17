// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <iostream>
#include <microfmt/inspector/address_space.hpp>
#include <microfmt/inspector/symbol_resolver.hpp>
#include <microfmt/sinks/stdio.hpp>

#if UINTPTR_MAX < UINT64_MAX

int main() { return 0; }

#else

// ----------------------------------------------------------------------------
// Mock Kernel / Platform Symbol Resolver Trait
// ----------------------------------------------------------------------------

struct mock_kernel_symbol_tag {};

struct mock_kernel_sym_entry {
  uintptr_t addr;
  const char *name;
  bool is_exact;
};

struct mock_kernel_image_entry {
  const char *name;
  uintptr_t load_base;
  size_t size;
  const mock_kernel_sym_entry *syms;
  size_t sym_count;
};

struct mock_kernel_context {
  const mock_kernel_image_entry *images;
  size_t image_count;
};

MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE

template <> struct microfmt::symbol_resolver_traits<mock_kernel_symbol_tag> {
  using context_type = mock_kernel_context;

  static bool resolve(microfmt::value_ref<const context_type> context,
                      uintptr_t addr, microfmt::span<char> /*scratch*/,
                      microfmt::raw_resolved_symbol &out_raw) noexcept {
    for (size_t i = 0; i < context->image_count; ++i) {
      const auto &img = context->images[i];
      if (addr >= img.load_base && addr < (img.load_base + img.size)) {
        out_raw.image_name = img.name;
        out_raw.image_load_base = img.load_base;

        // Search for nearest preceding or exact symbol
        for (size_t s = 0; s < img.sym_count; ++s) {
          const auto &sym = img.syms[s];
          if (addr >= sym.addr) {
            out_raw.symbol_name = sym.name;
            out_raw.symbol_base = sym.addr;
            out_raw.is_exact = sym.is_exact && (addr == sym.addr);
            break;
          }
        }
        return true;
      }
    }
    return false;
  }
};

MICROFMT_END_UNSAFE_BUFFER_USAGE

// ----------------------------------------------------------------------------
// Execution
// ----------------------------------------------------------------------------

int main() {
  static const mock_kernel_sym_entry vmlinux_syms[] = {
      {0xffff'8000'0010'2000ULL, "_ZN4core6kernel9scheduler7TaskFooEv", true},
      {0xffff'8000'0010'0000ULL, "_ZTVN8microfmt18SerialDeviceDriverE", true},
  };

  static const mock_kernel_image_entry images[] = {
      {"vmlinux", 0xffff'8000'0000'0000ULL, 0x0100'0000, vmlinux_syms, 2},
      {"nvgpu.ko", 0xffff'8000'0400'0000ULL, 0x0008'0000, nullptr, 0}, // Stripped driver
  };

  mock_kernel_context kctx{images, 2};
  microfmt::symbol_resolver_ref resolver(mock_kernel_symbol_tag{}, kctx);

  char scratch[64];
  microfmt::symbol_resolution_context symbol_context{scratch};

  microfmt::println("==================================================================");
  microfmt::println(" Type-Erased Symbol Resolver Demo");
  microfmt::println("==================================================================");

  // Exact function symbol
  microfmt::println("Exact Match   : {}",
                    microfmt::make_remote_symbol(0xffff'8000'0010'2000ULL, resolver, symbol_context));

  // Symbol with offset (+0x18)
  microfmt::println("Symbol Offset : {}",
                    microfmt::make_remote_symbol(0xffff'8000'0010'2018ULL, resolver, symbol_context));

  // Verbose formatting (module!symbol+offset)
  microfmt::println("Verbose       : {:#}",
                    microfmt::make_remote_symbol(0xffff'8000'0010'2018ULL, resolver, symbol_context));

  // Stripped module fallback (derived module offset)
  microfmt::println("Stripped Mod  : {}",
                    microfmt::make_remote_symbol(0xffff'8000'0401'4000ULL, resolver, symbol_context));

  // Unmapped address
  microfmt::println("Unmapped      : {}",
                    microfmt::make_remote_symbol(0x0000'0000'0040'0000ULL, resolver, symbol_context));

  return 0;
}

#endif
