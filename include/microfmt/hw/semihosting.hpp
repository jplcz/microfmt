#pragma once
#include "../microfmt.hpp"

namespace microfmt {

/**
 * @brief ARM Semihosting Sink for AArch64 freestanding environments.
 *
 * Routes formatted diagnostics directly to the host system via the
 * `hlt #0xf000` semihosting trap instruction.
 */
class MICROFMT_API_CLASS semihosting_sink {
public:
  constexpr semihosting_sink() noexcept = default;

  /**
   * @brief Writes a raw character buffer to the host via semihosting SYS_WRITE (0x05).
   * @param data Pointer to the character buffer.
   * @param len Number of bytes to write.
   */
  static void write(void *, microfmt::string_view sv) noexcept {
    if (sv.empty()) {
      return;
    }

    for (char c : sv) {
#if defined(__aarch64__)
      register uint64_t reg_sysnum __asm__("x0") = 0x03; // SYS_WRITEC operation code
      register const char *reg_ptr __asm__("x1") = &c;   // Pointer to the character

      __asm__ volatile("hlt #0xf000" : "+r"(reg_sysnum) : "r"(reg_ptr) : "memory", "cc");
#elif defined(__arm__) || defined(__thumb__)
      register uint32_t reg_sysnum __asm__("r0") = 0x03; // SYS_WRITEC operation code
      register const char *reg_ptr __asm__("r1") = &c;   // Pointer to the character

      __asm__ volatile("bkpt #0xab" : "+r"(reg_sysnum) : "r"(reg_ptr) : "memory", "cc");
#else
#error "Unsupported architecture for semihosting_sink"
#endif
    }
  }

  /**
   * @brief Adapts this semihosting sink into a type-erased `microfmt::sink`.
   * @return A lightweight `microfmt::sink` ready for `format_to`.
   */
  [[nodiscard]] constexpr sink as_sink() const noexcept {
    return sink{.ctx = nullptr, .write_fn = &semihosting_sink::write};
  }
};

} // namespace microfmt
