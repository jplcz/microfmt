// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <iostream>
#include <microfmt/inspector/address_space.hpp>
#include <microfmt/inspector/remote_diagnostics.hpp>
#include <microfmt/inspector/symbol_resolver.hpp>
#include <microfmt/sinks/stdio.hpp>

#if UINTPTR_MAX < UINT64_MAX

int main() { return 0; }

#else

// ============================================================================
// Mock Kernel Environment (Space + Resolver)
// ============================================================================

struct kernel_space_tag {};

struct kernel_memory_ctx {
  uintptr_t ram_base;
  size_t ram_size;
};

template <> struct microfmt::address_space_traits<kernel_space_tag> {
  using context_type = kernel_memory_ctx;

  static bool read_bytes(const void *ctx, uintptr_t addr, void *dest,
                         size_t size) noexcept {
    const auto &kmem = *static_cast<const kernel_memory_ctx *>(ctx);
    // Virtual kernel window: [0xffff800000000000 .. +ram_size]
    constexpr uintptr_t kKernelVBase = 0xffff'8000'0000'0000ULL;

    if (addr < kKernelVBase || (addr + size) > (kKernelVBase + kmem.ram_size)) {
      return false; // Fault outside valid mapped RAM
    }

    uintptr_t offset = addr - kKernelVBase;
    std::memcpy(dest, reinterpret_cast<const void *>(kmem.ram_base + offset),
                size);
    return true;
  }

  static bool read_string(const void *ctx, uintptr_t addr, char *dest,
                          size_t max_len, size_t &out_len,
                          bool &null_term) noexcept {
    const auto &kmem = *static_cast<const kernel_memory_ctx *>(ctx);
    constexpr uintptr_t kKernelVBase = 0xffff'8000'0000'0000ULL;

    if (addr < kKernelVBase || addr >= (kKernelVBase + kmem.ram_size)) {
      return false;
    }

    uintptr_t offset = addr - kKernelVBase;
    const auto *src = reinterpret_cast<const char *>(kmem.ram_base + offset);
    size_t avail = kmem.ram_size - offset;
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

struct kernel_sym_tag {};

template <> struct microfmt::symbol_resolver_traits<kernel_sym_tag> {
  using context_type = void;

  static bool resolve(const void *, uintptr_t addr, microfmt::span<char>,
                      microfmt::raw_resolved_symbol &out_raw) noexcept {
    // Exact handler symbols
    if (addr >= 0xffff'8000'0000'1000ULL && addr < 0xffff'8000'0000'1100ULL) {
      out_raw.image_name = "vmlinux";
      out_raw.image_load_base = 0xffff'8000'0000'0000ULL;
      out_raw.symbol_name = "_ZN6kernel2fs10ext4_writeEPcm";
      out_raw.symbol_base = 0xffff'8000'0000'1000ULL;
      out_raw.is_exact = (addr == 0xffff'8000'0000'1000ULL);
      return true;
    }

    // Unmapped/faulting region located inside kernel module region
    if (addr >= 0xffff'8000'0010'0000ULL && addr < 0xffff'8000'0011'0000ULL) {
      out_raw.image_name = "faulty_driver.ko";
      out_raw.image_load_base = 0xffff'8000'0010'0000ULL;
      return true;
    }

    return false;
  }
};

// ============================================================================
// Struct Definitions
// ============================================================================

struct FileOperations {
  uintptr_t read_fn;
  uintptr_t write_fn;
  uintptr_t release_fn;
};

template <> struct microfmt::formatter<FileOperations> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const FileOperations &fops, const sink &out) const noexcept {
    microfmt::format_to(out, "fops(read={:#x}, write={:#x}, release={:#x})",
                        fops.read_fn, fops.write_fn, fops.release_fn);
  }
};

// ============================================================================
// Execution
// ============================================================================

int main() {
  alignas(16) uint8_t simulated_kernel_ram[4096];
  std::memset(simulated_kernel_ram, 0, sizeof(simulated_kernel_ram));

  // Base setup: kernel RAM maps [0xffff800000000000 .. +4096]
  constexpr uintptr_t kVBase = 0xffff'8000'0000'0000ULL;

  // Place valid FileOperations struct at kVBase + 0x200
  auto *fops = static_cast<FileOperations *>(
      static_cast<void *>(&simulated_kernel_ram[0x200]));
  fops->read_fn = 0;                  // null
  fops->write_fn = kVBase + 0x1000;   // points to kernel::fs::ext4_write
  fops->release_fn = kVBase + 0x1024; // inside ext4_write+0x24

  kernel_memory_ctx mem_ctx{
      .ram_base = reinterpret_cast<uintptr_t>(simulated_kernel_ram),
      .ram_size = sizeof(simulated_kernel_ram)};

  microfmt::address_space_ref space(kernel_space_tag{}, mem_ctx);
  microfmt::symbol_resolver_ref resolver =
      microfmt::symbol_resolver_ref::make<kernel_sym_tag>();

  char scratch[64];
  alignas(alignof(FileOperations))
      std::byte fops_scratch[sizeof(FileOperations)];

  microfmt::println(
      "==================================================================");
  microfmt::println(" Symbol-Aware Pointer Dereferencing & Fault Diagnostics");
  microfmt::println(
      "==================================================================");

  // Inspect valid struct
  auto valid_ref = microfmt::remote_diag_ref<FileOperations>(
      kVBase + 0x200, space, resolver, fops_scratch, scratch);
  microfmt::println("Valid Struct      : {}", valid_ref);

  // Dereference and symbolize function pointers
  FileOperations *loaded_fops = nullptr;
  if (valid_ref.load(loaded_fops)) {
    microfmt::remote_fn_ptr fn_write(loaded_fops->write_fn, resolver, scratch);
    microfmt::remote_fn_ptr fn_rel(loaded_fops->release_fn, resolver, scratch);

    microfmt::println("Write Hook        : {}", fn_write);
    microfmt::println("Release Hook      : {}", fn_rel);
    microfmt::println("Release (Verbose) : {:#}", fn_rel);
  }

  // Unmapped read inside module -> Module-relative fault diagnostic
  auto mod_fault_ref = microfmt::remote_diag_ref<FileOperations>(
      0xffff'8000'0010'4200ULL, space, resolver, fops_scratch, scratch);
  microfmt::println("Module Fault      : {}", mod_fault_ref);

  // Completely unmapped read -> Raw hex fault diagnostic
  auto bad_fault_ref = microfmt::remote_diag_ref<FileOperations>(
      0x0000'0000'dead'beefULL, space, resolver, fops_scratch, scratch);
  microfmt::println("Unmapped Fault    : {}", bad_fault_ref);

  return 0;
}

#endif
