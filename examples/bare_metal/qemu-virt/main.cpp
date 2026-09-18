#include <microfmt/hw/pl011_sink.hpp>
#include <microfmt/hw/semihosting.hpp>
#include <microfmt/microfmt.hpp>

extern "C" size_t strlen(const char *s) {
  const char *p = s;
  while (*p != '\0') {
    ++p;
  }
  return static_cast<size_t>(p - s);
}

extern "C" void *memchr(const void *ptr, int value, size_t num) {
  if (!ptr || num == 0)
    return nullptr;

  const unsigned char *p = static_cast<const unsigned char *>(ptr);
  unsigned char target = static_cast<unsigned char>(value);

  for (size_t i = 0; i < num; ++i) {
    if (p[i] == target) {
      return const_cast<void *>(static_cast<const void *>(p + i));
    }
  }

  return nullptr;
}

// ============================================================================
// Custom Hardware Register Abstraction & Formatter
// ============================================================================

struct pl011_flag_register {
  uint32_t raw;

  [[nodiscard]] constexpr bool tx_ff() const noexcept { return (raw & (1 << 5)) != 0; } // Transmit FIFO full
  [[nodiscard]] constexpr bool rx_fe() const noexcept { return (raw & (1 << 4)) != 0; } // Receive FIFO empty
  [[nodiscard]] constexpr bool busy() const noexcept { return (raw & (1 << 3)) != 0; }  // UART busy
};

// Specialize microfmt::formatter for the hardware register type
template <> struct microfmt::formatter<pl011_flag_register> {
  constexpr void parse(microfmt::format_parse_context &) noexcept {}

  void format(const pl011_flag_register &reg, const microfmt::sink &out) const noexcept {
    microfmt::format_to(out, "FR{raw={:#06x} [tx_full={}, rx_empty={}, busy={}]}", reg.raw, reg.tx_ff(), reg.rx_fe(),
                        reg.busy());
  }
};

// ============================================================================
// Advanced Hexdump Utility for Memory Inspection
// ============================================================================

void print_memory_hexdump(const microfmt::sink &sink, uintptr_t base_addr, size_t size) noexcept {
  const uint8_t *ptr = reinterpret_cast<const uint8_t *>(base_addr);

  for (size_t i = 0; i < size; i += 16) {
    uintptr_t current_row_addr = base_addr + i;

    // Print address header
    microfmt::format_to(sink, "{:016x} | ", current_row_addr);

    // Print hex bytes
    for (size_t j = 0; j < 16; ++j) {
      if (i + j < size) {
        microfmt::format_to(sink, "{:02x} ", ptr[i + j]);
      } else {
        sink.write("   ");
      }
    }

    sink.write("| ");

    // Print ASCII representation
    for (size_t j = 0; j < 16 && i + j < size; ++j) {
      char c = static_cast<char>(ptr[i + j]);
      sink.put((c >= 32 && c < 127) ? c : '.');
    }

    sink.write("\n");
  }
}

// ============================================================================
// Kernel Entry Point
// ============================================================================

extern "C" void kernel_main() {
  microfmt::pl011_sink uart(0x09000000, true);
  uart.enable();

  microfmt::semihosting_sink semihosting;

  const auto sink = uart.as_sink();
  const auto semihosting_sink = semihosting.as_sink();

  // Clean, direct formatting style
  microfmt::format_to(sink, "\n==================================================\n");
  microfmt::format_to(sink, " [KERNEL] AArch64 Boot Diagnostics Initialized\n");
  microfmt::format_to(sink, "==================================================\n");

  uint64_t current_el = 0;
  __asm__ volatile("mrs %0, CurrentEL" : "=r"(current_el));
  current_el >>= 2;

  uint64_t mpidr = 0;
  __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));

  microfmt::format_to(sink, "CPU Core Info:\n");
  microfmt::format_to(sink, "  -> Exception Level : EL{}\n", current_el);
  microfmt::format_to(sink, "  -> MPIDR Raw       : {:#018x}\n", mpidr);
  microfmt::format_to(sink, "  -> Affinity (Cluster/Core): {}/{}\n\n", (mpidr >> 8) & 0xFF, mpidr & 0xFF);

  volatile uint32_t *fr_reg = reinterpret_cast<volatile uint32_t *>(0x09000000 + 0x18);
  pl011_flag_register uart_fr{*fr_reg};

  microfmt::format_to(sink, "Hardware Status:\n");
  microfmt::format_to(sink, "  -> UART State: {}\n\n", uart_fr);

  microfmt::format_to(sink, "Memory Inspection (ROM @ 0x40000000):\n");
  print_memory_hexdump(sink, 0x40000000, 64);

  microfmt::format_to(semihosting_sink, "Hello via semihosting\n");

  microfmt::format_to(sink, "\nSystem entering low-power idle state (WFI)...\n");

  while (true) {
    __asm__ volatile("wfi");
  }
}
