// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <cstring>
#include <string_view>

#include "microfmt/microfmt.hpp"
#include "microfmt/sinks/stdio.hpp"

namespace bench::stack {

inline constexpr uint8_t STACK_CANARY_BYTE = 0xA5;
inline constexpr size_t ARENA_SIZE = 16384;

#if defined(__GNUC__) || defined(__clang__)
#define NOINLINE [[gnu::noinline]]
#else
#define NOINLINE
#endif

struct alignas(16) stack_arena {
  uint8_t memory[ARENA_SIZE];
};

static stack_arena g_arena;

[[gnu::always_inline]] inline uintptr_t get_sp() noexcept {
#if defined(__x86_64__)
  uintptr_t sp;
  asm volatile("mov %%rsp, %0" : "=r"(sp));
  return sp;
#elif defined(__aarch64__)
  uintptr_t sp;
  asm volatile("mov %0, sp" : "=r"(sp));
  return sp;
#elif defined(__arm__) || defined(__thumb__)
  uintptr_t sp;
  asm volatile("mov %0, r13" : "=r"(sp));
  return sp;
#elif defined(__riscv)
  uintptr_t sp;
  asm volatile("mv %0, sp" : "=r"(sp));
  return sp;
#else
  volatile uint8_t local = 0;
  return reinterpret_cast<uintptr_t>(&local);
#endif
}

inline void paint_arena(stack_arena &arena) noexcept {
  std::memset(arena.memory, STACK_CANARY_BYTE, sizeof(arena.memory));
}

inline size_t measure_high_water_mark(const stack_arena &arena) noexcept {
  const uint8_t *top = arena.memory + sizeof(arena.memory);
  const uint8_t *ptr = arena.memory;

  while (ptr < top && *ptr == STACK_CANARY_BYTE) {
    ++ptr;
  }

  return static_cast<size_t>(top - ptr);
}

NOINLINE void run_on_isolated_stack(stack_arena &arena,
                                    void (*func)()) noexcept {
  paint_arena(arena);

  uintptr_t new_sp =
      (reinterpret_cast<uintptr_t>(arena.memory + sizeof(arena.memory)) &
       ~0xFULL);

#if defined(__x86_64__)
  asm volatile("mov %%rsp, %%r12\n\t"
               "mov %0, %%rsp\n\t"
               "call *%1\n\t"
               "mov %%r12, %%rsp\n\t"
               :
               : "r"(new_sp), "r"(func)
               : "r12", "memory");
#elif defined(__aarch64__)
  asm volatile("mov x19, sp\n\t"
               "mov sp, %0\n\t"
               "blr %1\n\t"
               "mov sp, x19\n\t"
               :
               : "r"(new_sp), "r"(func)
               : "x19", "memory");
#elif defined(__arm__)
  asm volatile("mov r4, r13\n\t"
               "mov r13, %0\n\t"
               "blx %1\n\t"
               "mov r13, r4\n\t"
               :
               : "r"(new_sp), "r"(func)
               : "r4", "memory");
#else
  volatile uint8_t stack_space[1024];
  std::memset((void *)stack_space, STACK_CANARY_BYTE, sizeof(stack_space));
  func();
#endif
}

} // namespace bench::stack

// =============================================================================
// Probes
// =============================================================================

template <typename T>
[[gnu::always_inline]] inline void do_not_optimize(T const &val) noexcept {
#if defined(__GNUC__) || defined(__clang__)
  asm volatile("" : : "m"(val) : "memory");
#else
  (void)val;
#endif
}

NOINLINE void probe_baseline() {
  volatile int x = 0;
  do_not_optimize(x);
}

NOINLINE void probe_microfmt_runtime_10_args() {
  uint8_t u8 = 0xFF;
  int16_t i16 = -32768;
  uint32_t u32 = 123456789;
  const char *str = "System";
  std::string_view sv = "Active";
  bool b = true;
  uint64_t u64 = 0xDEADBEEFCAFEBABE;
  void *ptr = (void *)0x20000000;
  char c = 'X';
  uint32_t code = 404;

  // Optimization barrier on arguments to prevent constant folding
  do_not_optimize(u8);
  do_not_optimize(i16);
  do_not_optimize(u32);
  do_not_optimize(str);
  do_not_optimize(sv);
  do_not_optimize(b);
  do_not_optimize(u64);
  do_not_optimize(ptr);
  do_not_optimize(c);
  do_not_optimize(code);

  auto sink = microfmt::null_sink();
  microfmt::format_to(sink.as_sink(),
                      "u8:{}, i16:{}, u32:{}, str:{}, sv:{}, b:{}, u64:{}, "
                      "ptr:{}, c:{}, code:{}",
                      u8, i16, u32, str, sv, b, u64, ptr, c, code);
}

NOINLINE void probe_microfmt_compiled_10_args() {
  uint8_t u8 = 0xFF;
  int16_t i16 = -32768;
  uint32_t u32 = 123456789;
  const char *str = "System";
  std::string_view sv = "Active";
  bool b = true;
  uint64_t u64 = 0xDEADBEEFCAFEBABE;
  void *ptr = (void *)0x20000000;
  char c = 'X';
  uint32_t code = 404;

  do_not_optimize(u8);
  do_not_optimize(i16);
  do_not_optimize(u32);
  do_not_optimize(str);
  do_not_optimize(sv);
  do_not_optimize(b);
  do_not_optimize(u64);
  do_not_optimize(ptr);
  do_not_optimize(c);
  do_not_optimize(code);

  auto sink = microfmt::null_sink();
  microfmt::format_to(sink.as_sink(),
                      MICROFMT_STRING("u8:{}, i16:{}, u32:{}, str:{}, sv:{}, "
                                      "b:{}, u64:{}, ptr:{}, c:{}, code:{}"),
                      u8, i16, u32, str, sv, b, u64, ptr, c, code);
}

// =============================================================================
// Main Runner
// =============================================================================

struct bench_case {
  const char *name;
  void (*fn)();
};

int main() {
  microfmt::println(MICROFMT_STRING(
      "==================================================================="));
  microfmt::println(MICROFMT_STRING(
      "   microfmt Runtime Stack High-Water Mark Benchmark (1-Byte Res)   "));
  microfmt::println(MICROFMT_STRING(
      "==================================================================="));

  bench::stack::run_on_isolated_stack(bench::stack::g_arena, probe_baseline);
  size_t baseline_stack =
      bench::stack::measure_high_water_mark(bench::stack::g_arena);

  microfmt::println(MICROFMT_STRING("Baseline Call Overhead: {} Bytes"),
                    baseline_stack);
  microfmt::println(MICROFMT_STRING(
      "-------------------------------------------------------------------"));
  microfmt::println(MICROFMT_STRING("{:<45} | {:>10} | {:>12}"),
                    "Benchmark Probe", "Peak Stack", "Net Overhead");
  microfmt::println(MICROFMT_STRING(
      "-------------------------------------------------------------------"));

  bench_case cases[] = {
      {"microfmt runtime vformat_to (10 args)", probe_microfmt_runtime_10_args},
      {"microfmt compiled MICROFMT_STRING (10 args)",
       probe_microfmt_compiled_10_args},
  };

  for (const auto &c : cases) {
    bench::stack::run_on_isolated_stack(bench::stack::g_arena, c.fn);
    size_t total_stack =
        bench::stack::measure_high_water_mark(bench::stack::g_arena);
    size_t net_overhead =
        (total_stack >= baseline_stack) ? (total_stack - baseline_stack) : 0;

    microfmt::println(MICROFMT_STRING("{:<45} | {:>8} B | {:>10} B"), c.name,
                      total_stack, net_overhead);
  }

  microfmt::println(MICROFMT_STRING(
      "==================================================================="));
  return 0;
}