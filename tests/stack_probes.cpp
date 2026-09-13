#include <cstdint>
#include <microfmt/microfmt.hpp>
#include <string_view>

namespace {

// Minimal zero-allocation sink that prevents optimizing away writes
struct volatile_sink {
  volatile uint8_t dummy{0};

  [[nodiscard]] microfmt::sink as_sink() noexcept {
    return microfmt::sink{this, [](void *ctx, std::string_view sv) noexcept {
                            auto *self = static_cast<volatile_sink *>(ctx);
                            for (char c : sv) {
                              self->dummy = static_cast<uint8_t>(c);
                            }
                          }};
  }
};

} // namespace

extern "C" {

// Probe 1: Zero arguments (pure string literal pass-through)
__attribute__((noinline)) void probe_stack_0_args(void) {
  volatile_sink vs;
  microfmt::format_to(vs.as_sink(), "Fixed panic message\n");
}

// Probe 2: 2 standard registers / integers
__attribute__((noinline)) void probe_stack_2_integers(uint32_t a, uint64_t b) {
  volatile_sink vs;
  microfmt::format_to(vs.as_sink(), "A: 0x{:08x}, B: 0x{:016x}\n", a, b);
}

// Probe 3: 4 mixed arguments (standard crash dump line)
__attribute__((noinline)) void
probe_stack_4_mixed(uint32_t code, const char *name, uintptr_t pc, void *sp) {
  volatile_sink vs;
  microfmt::format_to(vs.as_sink(), "[#{:02x}] {}: PC=0x{:016x} SP={}\n", code,
                      name, pc, sp);
}

// Probe 4: 8 mixed arguments (full CPU state / register context)
__attribute__((noinline)) void probe_stack_8_context(uint64_t r0, uint64_t r1,
                                                     uint64_t r2, uint64_t r3,
                                                     uint64_t r4, uint64_t r5,
                                                     uint64_t r6, uint64_t r7) {
  volatile_sink vs;
  microfmt::format_to(vs.as_sink(),
                      "R0: {:016x} R1: {:016x} R2: {:016x} R3: {:016x} "
                      "R4: {:016x} R5: {:016x} R6: {:016x} R7: {:016x}\n",
                      r0, r1, r2, r3, r4, r5, r6, r7);
}

// Probe 5: 64-byte stack buffer sink write
__attribute__((noinline)) void probe_stack_buffer_sink(uint32_t val) {
  microfmt::buffer_sink<64> buf;
  microfmt::format_to(buf.as_sink(), "Value = 0x{:08x}\n", val);
}

} // extern "C"
