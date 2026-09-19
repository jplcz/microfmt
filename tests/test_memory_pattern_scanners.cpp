// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/inspector/advanced_scanners.hpp>
#include <microfmt/inspector/memory_pattern_scanner.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

struct stateless_range_scanner_tag {};

template <>
struct microfmt::value_range_scanner_traits<stateless_range_scanner_tag,
                                            uint32_t> {
  using context_type = void;

  static microfmt::expected<microfmt::memory_scan_result,
                            microfmt::address_space_error>
  scan(uintptr_t start, size_t, const uint32_t &, const uint32_t &,
       size_t) noexcept {
    return microfmt::memory_scan_result{true, start};
  }
};

namespace {

struct record {
  uint32_t magic;
  uint16_t state;
  uint16_t priority;
};

bool matches_record(const void *bytes, void *opaque) noexcept {
  record candidate{};
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  std::memcpy(&candidate, bytes, sizeof(candidate));
  RELOCO_END_UNSAFE_BUFFER_USAGE;
  const auto expected = *static_cast<const uint32_t *>(opaque);
  return candidate.magic == expected && candidate.state < 8 &&
         candidate.priority < 32;
}

using pattern_scanner =
    microfmt::memory_pattern_scanner<microfmt::linear_memory_scanner_tag>;
using range_scanner = microfmt::value_range_scanner<
    microfmt::linear_value_range_scanner_tag<uint32_t>, uint32_t>;

} // namespace

TEST(MemoryPatternScannerTest, FindsExactAndMaskedCrossChunkPatterns) {
  const std::array<std::byte, 10> memory{
      std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0xde},
      std::byte{0xad}, std::byte{0x44}, std::byte{0xef}, std::byte{0x08},
      std::byte{0x09}, std::byte{0x0a}};
  std::array<std::byte, 5> scratch{};
  pattern_scanner scanner{microfmt::linear_memory_scanner_context{
      microfmt::address_space_ref{microfmt::local_space_tag{}},
      microfmt::span<std::byte>(scratch.data(), scratch.size())}};

  const std::array<std::byte, 4> exact{
      std::byte{0xde}, std::byte{0xad}, std::byte{0x44}, std::byte{0xef}};
  auto result = scanner.ref().scan(
      reinterpret_cast<uintptr_t>(memory.data()), memory.size(),
      microfmt::span<const std::byte>(exact.data(), exact.size()));
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->found);
  EXPECT_EQ(result->address, reinterpret_cast<uintptr_t>(memory.data()) + 3);

  const std::array<std::byte, 4> masked{
      std::byte{0xde}, std::byte{0xad}, std::byte{0x00}, std::byte{0xef}};
  const std::array<std::byte, 4> mask{
      std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0xff}};
  result = scanner.ref().scan(reinterpret_cast<uintptr_t>(memory.data()),
                              memory.size(),
                              microfmt::span<const std::byte>(masked.data(),
                                                              masked.size()),
                              microfmt::span<const std::byte>(mask.data(),
                                                              mask.size()));
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->found);
  EXPECT_EQ(result->address, reinterpret_cast<uintptr_t>(memory.data()) + 3);
}

TEST(MemoryPatternScannerTest, ValidatesPatternAndScratchSizes) {
  const std::array<std::byte, 4> memory{};
  std::array<std::byte, 2> scratch{};
  pattern_scanner scanner{microfmt::linear_memory_scanner_context{
      microfmt::address_space_ref{microfmt::local_space_tag{}},
      microfmt::span<std::byte>(scratch.data(), scratch.size())}};
  const std::array<std::byte, 3> pattern{};
  const std::array<std::byte, 2> mask{};

  const auto small = scanner.ref().scan(
      reinterpret_cast<uintptr_t>(memory.data()), memory.size(),
      microfmt::span<const std::byte>(pattern.data(), pattern.size()));
  ASSERT_FALSE(small);
  EXPECT_EQ(small.error(), microfmt::address_space_error::invalid_buffer);

  const auto bad_mask = scanner.ref().scan(
      reinterpret_cast<uintptr_t>(memory.data()), memory.size(),
      microfmt::span<const std::byte>(pattern.data(), 2),
      microfmt::span<const std::byte>(mask.data(), 1));
  ASSERT_FALSE(bad_mask);
  EXPECT_EQ(bad_mask.error(),
            microfmt::address_space_error::invalid_buffer);
}

TEST(AdvancedScannersTest, FindsValueWithinInclusiveRange) {
  const std::array<uint32_t, 6> values{1, 4, 17, 44, 101, 8};
  std::array<std::byte, 12> scratch{};
  range_scanner scanner{microfmt::linear_memory_scanner_context{
      microfmt::address_space_ref{microfmt::local_space_tag{}},
      microfmt::span<std::byte>(scratch.data(), scratch.size())}};

  const auto result = scanner.ref().scan(
      reinterpret_cast<uintptr_t>(values.data()),
      values.size() * sizeof(values[0]), 40U, 50U);
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->found);
  EXPECT_EQ(result->address,
            reinterpret_cast<uintptr_t>(values.data()) + 3 * sizeof(uint32_t));
}

TEST(AdvancedScannersTest, SupportsStatelessRangeTraits) {
  const auto scanner =
      microfmt::value_range_scanner<stateless_range_scanner_tag,
                                     uint32_t>::ref();
  const auto result = scanner.scan(0x1234, 16, 1U, 2U);
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->found);
  EXPECT_EQ(result->address, 0x1234U);
}

TEST(AdvancedScannersTest, FindsDependentRecordWithBoundedWindow) {
  const std::array<record, 3> records{{
      {0x11111111U, 1, 2},
      {0x5441534bU, 3, 12},
      {0x22222222U, 9, 40},
  }};
  std::array<std::byte, sizeof(record) * 2> scratch{};
  microfmt::dependent_value_scanner<
      microfmt::linear_dependent_value_scanner_tag>
      scanner{microfmt::linear_memory_scanner_context{
          microfmt::address_space_ref{microfmt::local_space_tag{}},
          microfmt::span<std::byte>(scratch.data(), scratch.size())}};
  uint32_t magic = 0x5441534bU;

  const auto invalid = scanner.ref().scan(
      reinterpret_cast<uintptr_t>(records.data()),
      records.size() * sizeof(records[0]), 0, sizeof(record),
      &matches_record, &magic);
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error(),
            microfmt::address_space_error::invalid_buffer);

  const auto result = scanner.ref().scan(
      reinterpret_cast<uintptr_t>(records.data()),
      records.size() * sizeof(records[0]), sizeof(record), sizeof(record),
      &matches_record, &magic);
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->found);
  EXPECT_EQ(result->address,
            reinterpret_cast<uintptr_t>(records.data()) + sizeof(record));
}
