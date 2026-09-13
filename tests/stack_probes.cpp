#include <cstdint>
#include <cstdio>
#include <microfmt/microfmt.hpp>
#include <string_view>

namespace {

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

// ============================================================================
// microfmt Probes
// ============================================================================

__attribute__((noinline)) void probe_microfmt_0_args(void) {
  volatile_sink vs;
  microfmt::format_to(vs.as_sink(), "Fixed panic message\n");
}

__attribute__((noinline)) void probe_microfmt_2_integers(uint32_t a,
                                                         uint64_t b) {
  volatile_sink vs;
  microfmt::format_to(vs.as_sink(), "A: 0x{:08x}, B: 0x{:016x}\n", a, b);
}

__attribute__((noinline)) void probe_microfmt_4_mixed(uint32_t code,
                                                      const char *name,
                                                      uintptr_t pc, void *sp) {
  volatile_sink vs;
  microfmt::format_to(vs.as_sink(), "[#{:02x}] {}: PC=0x{:016x} SP={}\n", code,
                      name, pc, sp);
}

__attribute__((noinline)) void
probe_microfmt_8_context(uint64_t r0, uint64_t r1, uint64_t r2, uint64_t r3,
                         uint64_t r4, uint64_t r5, uint64_t r6, uint64_t r7) {
  volatile_sink vs;
  microfmt::format_to(vs.as_sink(),
                      "R0: {:016x} R1: {:016x} R2: {:016x} R3: {:016x} "
                      "R4: {:016x} R5: {:016x} R6: {:016x} R7: {:016x}\n",
                      r0, r1, r2, r3, r4, r5, r6, r7);
}

__attribute__((noinline)) void
probe_stack_6_mixed_log(uint32_t timestamp, const char *module, char severity,
                        int32_t error_code, void *caller_pc, bool is_fatal) {
  volatile_sink vs;
  microfmt::format_to(vs.as_sink(), "[{}] {}: ({}) err={} pc={} fatal={}\n",
                      timestamp, module, severity, error_code, caller_pc,
                      is_fatal);
}

__attribute__((noinline)) void probe_stack_10_mixed_system_state(
    uint8_t id, int16_t temp, uint32_t voltage, const char *sensor_name,
    std::string_view status, bool calibrated, uint64_t uptime, void *dma_buffer,
    char rev_letter, uint32_t checksum) {
  volatile_sink vs;
  microfmt::format_to(vs.as_sink(),
                      "ID:#{:02x} T={} V={}mV S='{}' ST='{}' CAL={} UP={} "
                      "DMA={} REV={} CS=0x{:08x}\n",
                      id, temp, voltage, sensor_name, status, calibrated,
                      uptime, dma_buffer, rev_letter, checksum);
}

// ============================================================================
// libc snprintf Probes (Identical Scenarios)
// ============================================================================

__attribute__((noinline)) void probe_libc_snprintf_0_args(void) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "Fixed panic message\n");
}

__attribute__((noinline)) void probe_libc_snprintf_2_integers(uint32_t a,
                                                              uint64_t b) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "A: 0x%08x, B: 0x%016llx\n",
                static_cast<unsigned int>(a),
                static_cast<unsigned long long>(b));
}

__attribute__((noinline)) void probe_libc_snprintf_4_mixed(uint32_t code,
                                                           const char *name,
                                                           uintptr_t pc,
                                                           void *sp) {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "[#%02x] %s: PC=0x%016llx SP=%p\n",
                static_cast<unsigned int>(code), name,
                static_cast<unsigned long long>(pc), sp);
}

__attribute__((noinline)) void
probe_libc_snprintf_8_context(uint64_t r0, uint64_t r1, uint64_t r2,
                              uint64_t r3, uint64_t r4, uint64_t r5,
                              uint64_t r6, uint64_t r7) {
  char buf[192];
  std::snprintf(
      buf, sizeof(buf),
      "R0: %016llx R1: %016llx R2: %016llx R3: %016llx "
      "R4: %016llx R5: %016llx R6: %016llx R7: %016llx\n",
      static_cast<unsigned long long>(r0), static_cast<unsigned long long>(r1),
      static_cast<unsigned long long>(r2), static_cast<unsigned long long>(r3),
      static_cast<unsigned long long>(r4), static_cast<unsigned long long>(r5),
      static_cast<unsigned long long>(r6), static_cast<unsigned long long>(r7));
}

__attribute__((noinline)) void
probe_libc_snprintf_6_mixed_log(uint32_t timestamp, const char *module,
                                char severity, int32_t error_code,
                                void *caller_pc, bool is_fatal) {
  char buf[128];
  std::snprintf(buf, sizeof(buf), "[%u] %s: (%c) err=%d pc=%p fatal=%s\n",
                timestamp, module, severity, error_code, caller_pc,
                is_fatal ? "true" : "false");
}

__attribute__((noinline)) void probe_libc_snprintf_10_mixed_system_state(
    uint8_t id, int16_t temp, uint32_t voltage, const char *sensor_name,
    const char *status_cstr, bool calibrated, uint64_t uptime, void *dma_buffer,
    char rev_letter, uint32_t checksum) {
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "ID:#%02x T=%d V=%umV S='%s' ST='%s' CAL=%s UP=%llu DMA=%p "
                "REV=%c CS=0x%08x\n",
                id, temp, voltage, sensor_name, status_cstr,
                calibrated ? "true" : "false",
                static_cast<unsigned long long>(uptime), dma_buffer, rev_letter,
                checksum);
}

} // extern "C"
