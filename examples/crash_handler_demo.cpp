// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Standalone crash-handler demo: installs a real SIGSEGV/SIGBUS handler that
// prints a register dump (via ucontext_adapter.hpp + register_view.hpp) and
// resolves the faulting PC to a module/symbol (via dl_symbol_resolver.hpp),
// then deliberately triggers a real segfault to exercise it end-to-end.
//
// The handler writes only through `microfmt::fd_sink(STDERR_FILENO)`, which
// calls raw `write(2)` directly with no libc stdio buffering, locking, or
// allocation, keeping it async-signal-safe.

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <ucontext.h>
#include <unistd.h>

#include <microfmt/inspector/dl_symbol_resolver.hpp>
#include <microfmt/inspector/register_view.hpp>
#include <microfmt/inspector/ucontext_adapter.hpp>
#include <microfmt/sinks/stdio.hpp>

// ============================================================================
// Architecture Selection
// ============================================================================

#if defined(__x86_64__)
using arch_abi_traits = microfmt::x86_64_abi_traits;
namespace arch_dwarf = microfmt::dwarf::x86_64;
#elif defined(__i386__)
using arch_abi_traits = microfmt::x86_abi_traits;
namespace arch_dwarf = microfmt::dwarf::x86;
#elif defined(__aarch64__)
using arch_abi_traits = microfmt::aarch64_abi_traits;
namespace arch_dwarf = microfmt::dwarf::aarch64;
#elif defined(__arm__)
using arch_abi_traits = microfmt::arm_abi_traits;
namespace arch_dwarf = microfmt::dwarf::arm32;
#elif defined(__riscv) && __riscv_xlen == 64
using arch_abi_traits = microfmt::riscv64_abi_traits;
namespace arch_dwarf = microfmt::dwarf::riscv;
#elif defined(__riscv)
using arch_abi_traits = microfmt::riscv32_abi_traits;
namespace arch_dwarf = microfmt::dwarf::riscv;
#else
#error "Unsupported architecture for this demo"
#endif

// ============================================================================
// Crash Handler
// ============================================================================

namespace {

void crash_handler(int sig, siginfo_t *info, void *ucontext_raw) noexcept {
  ucontext_t uctx = *static_cast<ucontext_t *>(ucontext_raw);

  // Raw write(2) via fd_sink: no stdio buffering, locking, or allocation.
  auto out = microfmt::fd_sink(STDERR_FILENO);

  microfmt::println(out, "");
  microfmt::println(out, "================ CRASH DETECTED ================");
  microfmt::println(out, "Signal      : {} ({})", sig, sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : "?");
  microfmt::println(out, "Fault Addr  : {:#x}", reinterpret_cast<uintptr_t>(info->si_addr));

  // Build a read-only register context over the delivered ucontext_t.
  const microfmt::address_space_ref space(microfmt::local_space_tag{});
  std::byte reg_scratch[16];
  auto reg_ctx = microfmt::make_mutable_ucontext_register_context_ref(uctx, space, reg_scratch);

  microfmt::println(out, "");
  microfmt::println(out, "-- Registers --");
  microfmt::println(out, "{}", microfmt::register_context_view<arch_abi_traits>(reg_ctx));

  // Resolve the faulting PC to a module/symbol via dladdr.
  uint64_t pc_val = 0;
  if (reg_ctx.read(arch_dwarf::pc, pc_val)) {
    auto resolver = microfmt::symbol_resolver_ref::make<microfmt::dl_symbol_resolver_tag>();
    char sym_scratch[256];
    microfmt::symbol_resolution_context sym_ctx(sym_scratch);
    microfmt::println(out, "");
    microfmt::println(out, "Faulting PC : {:#x} ({:#})", pc_val,
                      microfmt::make_remote_symbol(static_cast<uintptr_t>(pc_val), resolver, sym_ctx));
  }

  microfmt::println(out, "==================================================");

  // Terminate immediately; the process state after a real fault is not
  // safe to resume from and we must not return from this handler.
  _exit(1);
}

} // namespace

// ============================================================================
// Main: Install Handler, Then Deliberately Crash
// ============================================================================

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
// GCC statically proves this deliberate wild-pointer dereference is
// out-of-bounds once inlined; that is the whole point of this demo, so
// silence the (correct) warning instead of hiding the crash.
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
int crash_intentionally(int depth, volatile int *bad_ptr) {
  if (depth > 0)
    return crash_intentionally(depth - 1, bad_ptr) + 1;
  // Dereferencing a wild pointer triggers a real SIGSEGV, caught below.
  return *bad_ptr;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

int main() {
  struct sigaction sa{};
  sa.sa_sigaction = crash_handler;
  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGSEGV, &sa, nullptr);
  sigaction(SIGBUS, &sa, nullptr);

  microfmt::println("=========================================================="
                    "======================");
  microfmt::println("      microfmt Signal-Based Crash Handler Demo");
  microfmt::println("=========================================================="
                    "======================");
  microfmt::println("Installing SIGSEGV/SIGBUS handler, then deliberately faulting...");

  // The handler below terminates via _exit(), which skips libc's normal
  // flush-on-exit; flush stdout here (regular, non-signal-handler code) so
  // this banner isn't lost.
  std::fflush(stdout);

  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  auto *wild_ptr = reinterpret_cast<volatile int *>(static_cast<uintptr_t>(0x10));
  int result = crash_intentionally(3, wild_ptr);

  // Unreachable: crash_handler() always calls _exit() before we get here.
  microfmt::println("Unexpectedly survived the fault (result={})", result);
  return 0;
}
