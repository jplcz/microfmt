// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Real-hardware(-emulated) testbed for the ARM EXIDX unwinder backend.
//
// Unlike examples/exidx_unwinder_test.cpp and examples/chained_unwinder_demo.cpp
// (which unwind a hand-crafted, simulated `.ARM.exidx` table over a fake
// address space), this program is a genuine 32-bit ARM Linux userspace
// executable: it installs a real SIGSEGV handler, deliberately crashes a few
// stack frames deep, and unwinds the *real* call stack using:
//   - microfmt::dl_elf_enumerator_tag  (dl_iterate_phdr-backed ELF image
//     lookup, now resolving real .ARM.exidx bounds via PT_ARM_EXIDX)
//   - microfmt::arm_exidx_unwinder_tag (binary-searches the real
//     .ARM.exidx/.ARM.extab tables emitted by the cross-compiler)
//   - microfmt::dl_symbol_resolver_tag (dladdr-based symbol names)
//   - microfmt::make_mutable_ucontext_register_context_ref (real arm_r*
//     registers delivered by the kernel/qemu-user into the signal handler)
//
// It is meant to be cross-compiled for arm-linux-gnueabi and run either
// directly under `qemu-arm` or attached to from VS Code via qemu's GDB stub
// (see README.md and ../.vscode/launch.json).

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <tuple>
#include <ucontext.h>
#include <unistd.h>
#include <vector>

#include <microfmt/inspector/arm_exidx_unwinder.hpp>
#include <microfmt/inspector/dl_elf_enumerator.hpp>
#include <microfmt/inspector/dl_symbol_resolver.hpp>
#include <microfmt/inspector/frame_pointer.hpp>
#include <microfmt/inspector/register_view.hpp>
#include <microfmt/inspector/ucontext_adapter.hpp>
#include <microfmt/sinks/stdio.hpp>

#if !defined(__arm__)
#error "This testbed targets 32-bit ARM (arm-linux-gnueabi*) only."
#endif

namespace {

// ============================================================================
// Crash Handler: Real EXIDX Unwind Over the Live Process Image
// ============================================================================

void crash_handler(int sig, siginfo_t *info, void *ucontext_raw) noexcept {
  ucontext_t uctx = *static_cast<ucontext_t *>(ucontext_raw);

  // Raw write(2) via fd_sink: no stdio buffering, locking, or allocation, so
  // this stays async-signal-safe.
  auto out = microfmt::fd_sink(STDERR_FILENO);

  microfmt::println(out, "");
  microfmt::println(out, "================ CRASH DETECTED ================");
  microfmt::println(out, "Signal      : {} ({})", sig, sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : "?");
  microfmt::println(out, "Fault Addr  : {:#x}", reinterpret_cast<uintptr_t>(info->si_addr));

  // Real (non-simulated) process address space: reads go through
  // process_vm_readv/ptrace-free local memcpy since we're unwinding our own
  // live process.
  const microfmt::address_space_ref space(microfmt::local_space_tag{});
  std::byte reg_scratch[16];
  auto reg_ctx = microfmt::make_mutable_ucontext_register_context_ref(uctx, space, reg_scratch);

  // Full r0-r15/cpsr dump straight from the delivered ucontext_t, before
  // anything is mutated by unwinding. Requires the ucontext_adapter's
  // register reads to accept native 32-bit widths (uint32_t), not just
  // uint64_t, which is what let this print correctly on real ARM32 targets.
  microfmt::println(out, "");
  microfmt::println(out, "-- Registers --");
  microfmt::println(out, "{}", microfmt::register_context_view<microfmt::arm_abi_traits>(reg_ctx));

  // Real ELF image enumerator: walks dl_iterate_phdr() over the actual
  // loaded images (this executable, and libc/ld.so if dynamically linked),
  // resolving .ARM.exidx bounds from each image's PT_ARM_EXIDX segment.
  microfmt::elf_image_enumerator<microfmt::dl_elf_enumerator_tag> enumerator(microfmt::dl_elf_enumerator_context{});
  microfmt::elf_image_info img_storage{};
  microfmt::arm_exidx_unwinder_context exidx_ctx{
      .space = space, .enumerator = enumerator.ref(), .elf_img_storage = &img_storage};
  microfmt::frame_unwinder_ref unwinder(microfmt::arm_exidx_unwinder_tag<>{}, exidx_ctx);

  auto resolver = microfmt::symbol_resolver_ref::make<microfmt::dl_symbol_resolver_tag>();
  char sym_scratch[256];
  microfmt::symbol_resolution_context sym_ctx(sym_scratch);

  // frame_pointer_iterator tracks the current PC itself and feeds it back
  // into the unwinder's step() to locate the right .ARM.exidx entry, so no
  // register (LR or otherwise) needs to be pre-seeded with the crash PC; the
  // "initial_fp" argument is used only for display/validity of frame 0
  // (AAPCS doesn't guarantee r11 is used as a frame pointer, and qemu-user's
  // delivered ucontext_t often has arm_fp == 0 here, so SP is passed instead).
  uint32_t sp_val = 0;
  std::ignore = reg_ctx.read(microfmt::dwarf::arm32::sp, sp_val);
  const auto pc_val = static_cast<uintptr_t>(uctx.uc_mcontext.arm_pc);

  microfmt::println(out, "");
  microfmt::println(out, "-- Real EXIDX-Unwound Backtrace --");
  microfmt::frame_pointer_iterator it(unwinder, reg_ctx, static_cast<uintptr_t>(sp_val), pc_val);
  microfmt::remote_backtrace_view bt(it, resolver, sym_ctx);
  microfmt::println(out, "{:#}", bt);

  microfmt::println(out, "==================================================");

  // Terminate immediately; the process state after a real fault is not safe
  // to resume from, and we must not return from this handler.
  _exit(1);
}

} // namespace

// ============================================================================
// Main: Install Handler, Then Deliberately Crash a Few Frames Deep
// ============================================================================

namespace {

// Non-trivial C++ objects deliberately placed as stack locals in the
// crash-path frames below, so this testbed exercises more than
// trivially-laid-out POD frames: a virtual destructor (vtable pointer as a
// stack local's first word), a heap-owning std::string/std::vector/
// unique_ptr (extra callee-saved-register spills for their control blocks),
// and a base/derived hierarchy (constructor call chains of their own,
// distinct .ARM.exidx entries). None of this changes the crash itself (still
// a wild pointer deref) or the unwinder's contract (it walks return
// addresses/frame pointers, not C++ object lifetimes) -- it only makes each
// frame's stack layout realistic instead of minimal, which is the actual
// thing worth checking here (whether extra spilled registers/locals confuse
// the EXIDX-driven restore).
struct base_context {
  explicit base_context(const char *tag) : tag_(tag) {}
  virtual ~base_context() = default;
  [[nodiscard]] virtual const char *describe() const noexcept { return tag_.c_str(); }

  std::string tag_;
  std::vector<int> trail;
};

struct frame_context final : base_context {
  frame_context(const char *tag, int depth_value) : base_context(tag), depth(depth_value) {
    trail.push_back(depth_value);
  }
  [[nodiscard]] const char *describe() const noexcept override { return tag_.c_str(); }

  int depth;
  std::unique_ptr<int> marker = std::make_unique<int>(depth);
};

} // namespace

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
// GCC statically proves this deliberate wild-pointer dereference is
// out-of-bounds once inlined; that is the whole point of this demo, so
// silence the (correct) warning instead of hiding the crash.
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
// noinline keeps each level a distinct .ARM.exidx-covered frame instead of
// being folded away by the optimizer, so the unwinder has real depth to walk.
__attribute__((noinline)) int level_c(volatile int *bad_ptr) {
  frame_context ctx("level_c", 3);
  return *bad_ptr + static_cast<int>(ctx.trail.size());
}
__attribute__((noinline)) int level_b(volatile int *bad_ptr) {
  frame_context ctx("level_b", 2);
  return level_c(bad_ptr) + ctx.depth;
}
__attribute__((noinline)) int level_a(volatile int *bad_ptr) {
  frame_context ctx("level_a", 1);
  return level_b(bad_ptr) + ctx.depth;
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

  microfmt::println("==================================================================");
  microfmt::println(" ARM EXIDX Crash Testbed (real .ARM.exidx, real ucontext_t)");
  microfmt::println("==================================================================");
  microfmt::println("Installing SIGSEGV/SIGBUS handler, then deliberately faulting 3 frames deep...");
  std::fflush(stdout);

  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  auto *wild_ptr = reinterpret_cast<volatile int *>(static_cast<uintptr_t>(0x10));
  int result = level_a(wild_ptr);

  // Unreachable: crash_handler() always calls _exit() before we get here.
  microfmt::println("Unexpectedly survived the fault (result={})", result);
  return 0;
}
