// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <microfmt/inspector/memory_diff.hpp>
#include <microfmt/microfmt.hpp>
#include <microfmt/sinks/stdio.hpp>

namespace {

// Example firmware configuration struct captured before and after an update.
struct DeviceConfig {
  uint32_t magic;
  uint16_t version;
  uint8_t retry_count;
  uint8_t flags;
  char label[8];
};

} // namespace

int main() {
  const auto output = microfmt::stdout_sink();

  // ------------------------------------------------------------------------
  // 1. Diff two raw byte buffers (e.g. firmware image sectors) with a custom
  //    base address so the report reads like an absolute memory dump.
  // ------------------------------------------------------------------------
  std::puts("=== 1. Raw Buffer Diff ===");

  const std::array<uint8_t, 32> before{{0x45, 0x00, 0x00, 0x3c, 0x1c, 0x46, 0x40, 0x00, 0x40, 0x06, 0xb1,
                                        0xe6, 0xc0, 0xa8, 0x01, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
  auto after = before;
  after[16] = 0x01; // Sector's status byte flipped after the update.
  after[17] = 0x02;

  microfmt::span<const uint8_t> before_span(before.data(), before.size());
  microfmt::span<const uint8_t> after_span(after.data(), after.size());

  microfmt::format_to(output, "Sector diff @ 0x{:08x}:\n{}\n", 0x08000000,
                      microfmt::mem_diff(before_span, after_span, 0x08000000));

  // ------------------------------------------------------------------------
  // 2. Compare two struct snapshots to spot which fields changed after
  //    applying a configuration update, with a smaller row width so every
  //    diverging field lands in its own row.
  // ------------------------------------------------------------------------
  std::puts("=== 2. Struct Snapshot Diff ===");

  const DeviceConfig old_config{0xDEADBEEF, 0x0102, 3, 0x00, "PROD_v1"};
  DeviceConfig new_config = old_config;
  new_config.version = 0x0103;
  new_config.retry_count = 5;

  auto struct_diff = microfmt::mem_diff(
      microfmt::span<const uint8_t>(reinterpret_cast<const uint8_t *>(&old_config), sizeof(old_config)),
      microfmt::span<const uint8_t>(reinterpret_cast<const uint8_t *>(&new_config), sizeof(new_config)),
      reinterpret_cast<uintptr_t>(&old_config));
  struct_diff.bytes_per_row = 4;

  microfmt::format_to(output, "Config @ 0x{:08x}:\n{}\n", reinterpret_cast<uintptr_t>(&old_config), struct_diff);

  // ------------------------------------------------------------------------
  // 3. Format into a bounded stack buffer instead of streaming directly, for
  //    embedding the diff report into a larger log line.
  // ------------------------------------------------------------------------
  std::puts("=== 3. Format to Stack Buffer ===");

  auto buf = microfmt::format<512>("{}", microfmt::mem_diff(before_span, after_span));

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  std::printf("%.*s\n", static_cast<int>(buf.size()), buf.view().data());

  RELOCO_END_UNSAFE_BUFFER_USAGE;

  return 0;
}
