// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <cstdint>
#include <cstdio>
#include <microfmt/formatters/hexdump.hpp>
#include <microfmt/microfmt.hpp>
#include <string_view>

// Hardware callback example: streams formatted characters directly to
// stdout/UART
static void uart_write_callback(void * /*ctx*/, std::string_view sv) noexcept {
  std::fwrite(sv.data(), 1, sv.size(), stdout);
}

struct SystemConfig {
  uint32_t magic;
  uint16_t version;
  uint8_t flags;
  char tag[9];
};

int main() {
  // Sample 28-byte network packet payload
  const uint8_t packet[] = {0x45, 0x00, 0x00, 0x3c, 0x1c, 0x46, 0x40,
                            0x00, 0x40, 0x06, 0xb1, 0xe6, 0xc0, 0xa8,
                            0x01, 0x64, 0x48, 0x65, 0x6c, 0x6c, 0x6f,
                            0x20, 0x57, 0x6f, 0x72, 0x6c, 0x64, 0x21};

  // ------------------------------------------------------------------------
  // Stream hexdump directly to a custom sink (e.g. UART / stdout)
  // ------------------------------------------------------------------------
  std::puts("=== 1. Direct UART / Sink Stream ===");
  microfmt::sink uart_sink{nullptr, uart_write_callback};

  microfmt::format_to(
      uart_sink, "Packet Dump ({} bytes):\n{}", sizeof(packet),
      microfmt::hexdump(microfmt::span<const uint8_t>(packet, sizeof(packet))));

  // ------------------------------------------------------------------------
  // Format into a fixed-size stack buffer with a custom base address
  // ------------------------------------------------------------------------
  std::puts("\n=== 2. Format to Stack Buffer with Base Address ===");
  auto buf = microfmt::format<512>(
      "{}",
      microfmt::hexdump(microfmt::span<const uint8_t>(packet, sizeof(packet)),
                        /*base_address=*/0x20000000));

  std::printf("%.*s\n", static_cast<int>(buf.size()), buf.view().data());

  // ------------------------------------------------------------------------
  // Inspect raw struct memory
  // ------------------------------------------------------------------------
  std::puts("=== 3. Raw Struct Inspection ===");
  const SystemConfig config{0xDEADBEEF, 0x0102, 0x80, "PROD_v2"};

  auto struct_dump = microfmt::format<512>(
      "Config @ 0x{:08x}:\n{}", reinterpret_cast<uintptr_t>(&config),
      microfmt::hexdump(
          microfmt::span<const uint8_t>(
              reinterpret_cast<const uint8_t *>(&config), sizeof(config)),
          reinterpret_cast<uintptr_t>(&config)));

  std::printf("%.*s\n", static_cast<int>(struct_dump.size()),
              struct_dump.view().data());

  return 0;
}
