// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <microfmt/inspector/memory_diff.hpp>
#include <microfmt/microfmt.hpp>

namespace {

std::byte to_byte(int value) noexcept { return static_cast<std::byte>(value); }

} // namespace

TEST(MemoryDiffViewTest, CollapsesFullyIdenticalRegionsIntoSummary) {
  const std::array<uint8_t, 32> old_buf{{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                                        0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                                        0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f}};
  const auto &new_buf = old_buf;

  microfmt::span<const uint8_t> old_span(old_buf.data(), old_buf.size());
  microfmt::span<const uint8_t> new_span(new_buf.data(), new_buf.size());

  microfmt::buffer_sink<256> output;
  microfmt::format_to(output.as_sink(), "{}", microfmt::mem_diff(old_span, new_span, 0x1000));

  // 32 bytes at the default 16 bytes/row -> 2 identical rows collapsed.
  EXPECT_EQ(output.view(), "  [... 2 identical rows hidden ...]\n");
}

TEST(MemoryDiffViewTest, EmitsRowsForEveryDivergentByte) {
  const uint8_t old_buf[8] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
  const uint8_t new_buf[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

  microfmt::span<const uint8_t> old_span(old_buf, 8);
  microfmt::span<const uint8_t> new_span(new_buf, 8);

  auto diff = microfmt::mem_diff(old_span, new_span, 0x2000);
  diff.bytes_per_row = 8;

  microfmt::buffer_sink<256> output;
  microfmt::format_to(output.as_sink(), "{}", diff);

  // Byte columns are always rendered as uppercase hex, regardless of the
  // (lowercase) address column.
  EXPECT_EQ(output.view(),
            "  0x00002000: 00 01 02 03 04 05 06 07  -> FF FF FF FF FF FF FF FF \n");
}

TEST(MemoryDiffViewTest, CollapsesLeadingAndTrailingIdenticalRowsAroundDivergence) {
  uint8_t old_buf[12] = {0};
  uint8_t new_buf[12] = {0};
  // Row 1 (bytes 4-7) diverges; rows 0 and 2 stay identical.
  new_buf[4] = 0xAA;

  microfmt::span<const uint8_t> old_span(old_buf, 12);
  microfmt::span<const uint8_t> new_span(new_buf, 12);

  auto diff = microfmt::mem_diff(old_span, new_span, 0);
  diff.bytes_per_row = 4;

  microfmt::buffer_sink<256> output;
  microfmt::format_to(output.as_sink(), "{}", diff);

  EXPECT_EQ(output.view(), "  [... 1 identical rows hidden ...]\n"
                          "  0x00000004: 00 00 00 00  -> AA 00 00 00 \n"
                          "  [... 1 identical rows hidden ...]\n");
}

TEST(MemoryDiffViewTest, TruncatesToShorterOfTheTwoRegions) {
  const uint8_t old_buf[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  const uint8_t new_buf[4] = {1, 2, 3, 9};

  microfmt::span<const uint8_t> old_span(old_buf, 8);
  microfmt::span<const uint8_t> new_span(new_buf, 4);

  auto diff = microfmt::mem_diff(old_span, new_span, 0);
  diff.bytes_per_row = 4;

  microfmt::buffer_sink<128> output;
  microfmt::format_to(output.as_sink(), "{}", diff);

  // Only the first 4 bytes (min region size) are compared.
  EXPECT_EQ(output.view(), "  0x00000000: 01 02 03 04  -> 01 02 03 09 \n");
}

TEST(MemoryDiffViewTest, HandlesEmptyRegions) {
  microfmt::span<const uint8_t> empty_span(nullptr, size_t{0});

  microfmt::buffer_sink<64> output;
  microfmt::format_to(output.as_sink(), "{}", microfmt::mem_diff(empty_span, empty_span, 0x1000));

  EXPECT_EQ(output.view(), "");
}

TEST(MemoryDiffViewTest, TreatsZeroBytesPerRowAsDefaultRowSize) {
  const uint8_t old_buf[16] = {0};
  uint8_t new_buf[16] = {0};
  new_buf[0] = 1;

  microfmt::span<const uint8_t> old_span(old_buf, 16);
  microfmt::span<const uint8_t> new_span(new_buf, 16);

  auto diff = microfmt::mem_diff(old_span, new_span, 0);
  diff.bytes_per_row = 0; // Falls back to 16 bytes/row.

  microfmt::buffer_sink<128> output;
  microfmt::format_to(output.as_sink(), "{}", diff);

  EXPECT_EQ(output.view(), "  0x00000000: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  "
                          "-> 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 \n");
}

TEST(MemoryDiffViewTest, ConstructsFromMatchingTemplateSpanTypes) {
  const std::array<std::byte, 4> old_buf{{to_byte(0xde), to_byte(0xad), to_byte(0xbe), to_byte(0xef)}};
  const std::array<std::byte, 4> new_buf{{to_byte(0xde), to_byte(0xad), to_byte(0xc0), to_byte(0xde)}};

  microfmt::span<const std::byte> old_span(old_buf.data(), old_buf.size());
  microfmt::span<const std::byte> new_span(new_buf.data(), new_buf.size());

  auto diff = microfmt::mem_diff(old_span, new_span, 0);
  diff.bytes_per_row = 4;

  microfmt::buffer_sink<128> output;
  microfmt::format_to(output.as_sink(), "{}", diff);

  EXPECT_EQ(output.view(), "  0x00000000: DE AD BE EF  -> DE AD C0 DE \n");
}

TEST(MemoryDiffViewTest, ConstructsFromMixedSpanElementTypes) {
  const uint32_t old_words[2] = {0x03020100, 0x07060504};
  const uint32_t new_words[2] = {0x03020100, 0xffffffff};

  microfmt::span<const uint32_t> old_span(old_words, 2);
  microfmt::span<const std::byte> new_span(reinterpret_cast<const std::byte *>(new_words), sizeof(new_words));

  auto diff = microfmt::mem_diff(old_span, new_span, 0);

  EXPECT_EQ(diff.old_data.size(), sizeof(old_words));
  EXPECT_EQ(diff.new_data.size(), sizeof(new_words));
}

#if RELOCO_HAS_STD_SPAN
TEST(MemoryDiffViewTest, ConstructsFromStdSpanOverloads) {
  const std::array<uint8_t, 4> old_buf{{1, 2, 3, 4}};
  const std::array<uint8_t, 4> new_buf{{1, 2, 3, 5}};

  std::span<const uint8_t, 4> old_span(old_buf);
  std::span<const uint8_t, 4> new_span(new_buf);

  auto diff = microfmt::mem_diff(old_span, new_span, 0);
  diff.bytes_per_row = 4;

  microfmt::buffer_sink<128> output;
  microfmt::format_to(output.as_sink(), "{}", diff);

  EXPECT_EQ(output.view(), "  0x00000000: 01 02 03 04  -> 01 02 03 05 \n");
}

TEST(MemoryDiffViewTest, ConstructsFromStdSpanWithByteOverload) {
  const std::array<uint8_t, 4> old_buf{{1, 2, 3, 4}};
  const std::array<std::byte, 4> new_buf{{to_byte(1), to_byte(2), to_byte(3), to_byte(4)}};

  std::span<const uint8_t, 4> old_span(old_buf);
  std::span<const std::byte, 4> new_span(new_buf);

  auto diff = microfmt::mem_diff(old_span, new_span, 0);

  EXPECT_EQ(diff.old_data.size(), old_buf.size());
  EXPECT_EQ(diff.new_data.size(), new_buf.size());
}
#endif
