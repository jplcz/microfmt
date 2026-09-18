// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <microfmt/inspector/remote_memory_diff.hpp>
#include <microfmt/microfmt.hpp>

namespace {

// A stateful address space bounded to a fixed [base, base + size) window;
// reads outside the window fail, simulating an unmapped/faulted remote page.
struct bounded_space_tag {};

struct bounded_space_context {
  const uint8_t *data;
  uintptr_t base;
  size_t size;
};

} // namespace

template <> struct microfmt::address_space_traits<bounded_space_tag> {
  using context_type = bounded_space_context;

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

namespace {

microfmt::address_space_ref local_space() { return microfmt::address_space_ref(microfmt::local_space_tag{}); }

// Address column uses lowercase hex, zero-padded to (at least) 8 digits, same
// as `microfmt`'s built-in `{:08x}` integer formatting.
std::string hex_addr(uintptr_t addr) {
  auto buf = microfmt::format<32>("{:08x}", addr);
  return std::string(buf.view());
}

} // namespace

TEST(RemoteMemoryDiffViewTest, ReportsErrorWhenScratchBufferTooSmall) {
  const uint8_t old_buf[8] = {0};
  const uint8_t new_buf[8] = {0};

  std::byte scratch[4]; // Needs >= bytes_per_row * 2 == 32 bytes.

  auto diff = microfmt::remote_mem_diff(local_space(), reinterpret_cast<uintptr_t>(old_buf), local_space(),
                                        reinterpret_cast<uintptr_t>(new_buf), 8,
                                        microfmt::span<std::byte>(scratch, sizeof(scratch)));

  microfmt::buffer_sink<128> output;
  microfmt::format_to(output.as_sink(), "{}", diff);

  EXPECT_EQ(output.view(), "[ERROR: Scratch buffer too small for remote_memory_diff]");
}

TEST(RemoteMemoryDiffViewTest, CollapsesFullyIdenticalRegionsIntoSummary) {
  const std::array<uint8_t, 32> old_buf{{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                                        0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                                        0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f}};
  const auto &new_buf = old_buf;

  std::byte scratch[64];
  auto diff = microfmt::remote_mem_diff(local_space(), reinterpret_cast<uintptr_t>(old_buf.data()), local_space(),
                                        reinterpret_cast<uintptr_t>(new_buf.data()), old_buf.size(),
                                        microfmt::span<std::byte>(scratch, sizeof(scratch)));

  microfmt::buffer_sink<256> output;
  microfmt::format_to(output.as_sink(), "{}", diff);

  // 32 bytes at the default 16 bytes/row -> 2 identical rows collapsed.
  EXPECT_EQ(output.view(), "  [... 2 identical rows hidden ...]\n");
}

TEST(RemoteMemoryDiffViewTest, EmitsRowsForEveryDivergentByte) {
  const uint8_t old_buf[8] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
  const uint8_t new_buf[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

  std::byte scratch[16]; // bytes_per_row(8) * 2

  auto diff = microfmt::remote_mem_diff(local_space(), reinterpret_cast<uintptr_t>(old_buf), local_space(),
                                        reinterpret_cast<uintptr_t>(new_buf), 8,
                                        microfmt::span<std::byte>(scratch, sizeof(scratch)));
  diff.bytes_per_row = 8;

  microfmt::buffer_sink<256> output;
  microfmt::format_to(output.as_sink(), "{}", diff);

  const std::string expected = "  0x" + hex_addr(reinterpret_cast<uintptr_t>(new_buf)) +
                              ": 00 01 02 03 04 05 06 07  -> FF FF FF FF FF FF FF FF \n";
  EXPECT_EQ(output.view(), microfmt::string_view(expected));
}

TEST(RemoteMemoryDiffViewTest, CollapsesLeadingAndTrailingIdenticalRowsAroundDivergence) {
  uint8_t old_buf[12] = {0};
  uint8_t new_buf[12] = {0};
  // Row 1 (bytes 4-7) diverges; rows 0 and 2 stay identical.
  new_buf[4] = 0xAA;

  std::byte scratch[8]; // bytes_per_row(4) * 2

  auto diff = microfmt::remote_mem_diff(local_space(), reinterpret_cast<uintptr_t>(old_buf), local_space(),
                                        reinterpret_cast<uintptr_t>(new_buf), 12,
                                        microfmt::span<std::byte>(scratch, sizeof(scratch)));
  diff.bytes_per_row = 4;

  microfmt::buffer_sink<256> output;
  microfmt::format_to(output.as_sink(), "{}", diff);

  const std::string expected = "  [... 1 identical rows hidden ...]\n"
                              "  0x" +
                              hex_addr(reinterpret_cast<uintptr_t>(new_buf) + 4) +
                              ": 00 00 00 00  -> AA 00 00 00 \n"
                              "  [... 1 identical rows hidden ...]\n";
  EXPECT_EQ(output.view(), microfmt::string_view(expected));
}

TEST(RemoteMemoryDiffViewTest, TreatsZeroBytesPerRowAsDefaultRowSize) {
  const uint8_t old_buf[16] = {0};
  uint8_t new_buf[16] = {0};
  new_buf[0] = 1;

  std::byte scratch[32]; // Falls back to the default 16 bytes/row * 2.

  auto diff = microfmt::remote_mem_diff(local_space(), reinterpret_cast<uintptr_t>(old_buf), local_space(),
                                        reinterpret_cast<uintptr_t>(new_buf), 16,
                                        microfmt::span<std::byte>(scratch, sizeof(scratch)));
  diff.bytes_per_row = 0; // Falls back to 16 bytes/row.

  microfmt::buffer_sink<128> output;
  microfmt::format_to(output.as_sink(), "{}", diff);

  const std::string expected = "  0x" + hex_addr(reinterpret_cast<uintptr_t>(new_buf)) +
                              ": 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  "
                              "-> 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 \n";
  EXPECT_EQ(output.view(), microfmt::string_view(expected));
}

TEST(RemoteMemoryDiffViewTest, ReportsFaultForUnreadableRemoteRows) {
  const uint8_t old_data[8] = {0x00, 0x00, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44};
  const uint8_t new_data[4] = {0x00, 0x00, 0x00, 0x00}; // Second row is out of bounds.

  bounded_space_context old_ctx{old_data, 0x1000, sizeof(old_data)};
  bounded_space_context new_ctx{new_data, 0x1000, sizeof(new_data)};

  microfmt::address_space_ref old_space(bounded_space_tag{}, old_ctx);
  microfmt::address_space_ref new_space(bounded_space_tag{}, new_ctx);

  std::byte scratch[8]; // bytes_per_row(4) * 2

  auto diff = microfmt::remote_mem_diff(old_space, 0x1000, new_space, 0x1000, 8,
                                        microfmt::span<std::byte>(scratch, sizeof(scratch)));
  diff.bytes_per_row = 4;

  microfmt::buffer_sink<256> output;
  microfmt::format_to(output.as_sink(), "{}", diff);

  // The first row (identical) is collapsed; the second is unreadable in the
  // new space and is reported as a fault instead of a byte comparison.
  EXPECT_EQ(output.view(), "  [... 1 identical rows hidden ...]\n"
                          "  0x00001004: [REMOTE READ FAULT]\n");
}
