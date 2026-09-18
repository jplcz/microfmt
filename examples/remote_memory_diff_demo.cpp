// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <microfmt/inspector/remote_memory_diff.hpp>
#include <microfmt/microfmt.hpp>
#include <microfmt/sinks/stdio.hpp>

namespace {

// A tiny simulated "guest" address space backed by a host-owned byte buffer,
// standing in for a debuggee process or remote target. Reads outside the
// buffer fail, simulating an unmapped or faulted remote page.
struct guest_space_tag {};

struct guest_space_context {
  const uint8_t *data;
  uintptr_t base;
  size_t size;
};

} // namespace

template <> struct microfmt::address_space_traits<guest_space_tag> {
  using context_type = guest_space_context;

  static bool read_bytes(microfmt::value_ref<const context_type> context, uintptr_t addr, void *dest,
                        size_t size) noexcept {
    if (addr < context->base)
      return false;
    const uintptr_t offset = addr - context->base;
    if (offset > context->size || size > context->size - offset)
      return false;
    std::memcpy(dest, context->data + offset, size);
    return true;
  }

  static bool read_string(microfmt::value_ref<const context_type>, uintptr_t, char *, size_t, size_t &,
                          bool &) noexcept {
    return false;
  }
};

int main() {
  const auto output = microfmt::stdout_sink();

  // ------------------------------------------------------------------------
  // 1. Diff two snapshots of a live target process taken across two points
  //    in time (e.g. before/after a suspected memory-corruption bug), read
  //    directly through the local address space.
  // ------------------------------------------------------------------------
  std::puts("=== 1. Local Process Snapshot Diff ===");

  alignas(16) uint8_t region[32];
  for (size_t i = 0; i < sizeof(region); ++i)
    region[i] = static_cast<uint8_t>(i);

  // Snapshot "before" by copying the region out before mutating it.
  std::array<uint8_t, sizeof(region)> before{};
  std::memcpy(before.data(), region, sizeof(region));

  // Simulate corruption: a stray write flips a few bytes in the middle.
  region[16] = 0xff;
  region[17] = 0xff;

  const auto local_space = microfmt::address_space_ref(microfmt::local_space_tag{});
  std::byte scratch[64]; // >= bytes_per_row * 2 for the default 16-byte rows.

  auto local_diff = microfmt::remote_mem_diff(local_space, reinterpret_cast<uintptr_t>(before.data()), local_space,
                                              reinterpret_cast<uintptr_t>(region), sizeof(region),
                                              microfmt::span<std::byte>(scratch, sizeof(scratch)));

  microfmt::format_to(output, "{}\n", local_diff);

  // ------------------------------------------------------------------------
  // 2. Diff two remote target snapshots read through a custom address_space
  //    (e.g. two coredumps, or a debuggee re-read across a breakpoint),
  //    including a page that has since become unreadable in the new image.
  // ------------------------------------------------------------------------
  std::puts("=== 2. Remote Target Diff With a Faulted Page ===");

  uint8_t old_image[16];
  for (size_t i = 0; i < sizeof(old_image); ++i)
    old_image[i] = static_cast<uint8_t>(0x10 + i);

  uint8_t new_image[8]; // Only the first row is still mapped in the new image.
  std::memcpy(new_image, old_image, sizeof(new_image));
  new_image[4] = 0xAA; // One byte changed within the still-mapped row.

  guest_space_context old_ctx{old_image, 0x4000, sizeof(old_image)};
  guest_space_context new_ctx{new_image, 0x4000, sizeof(new_image)};

  microfmt::address_space_ref old_space(guest_space_tag{}, old_ctx);
  microfmt::address_space_ref new_space(guest_space_tag{}, new_ctx);

  std::byte remote_scratch[16]; // bytes_per_row(8) * 2
  auto remote_diff = microfmt::remote_mem_diff(old_space, 0x4000, new_space, 0x4000, sizeof(old_image),
                                               microfmt::span<std::byte>(remote_scratch, sizeof(remote_scratch)));
  remote_diff.bytes_per_row = 8;

  microfmt::format_to(output, "{}\n", remote_diff);

  return 0;
}
