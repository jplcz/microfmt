// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/inspector/remote_page_table_walker.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

struct physical_space_tag {};

struct physical_space_context {
  uintptr_t base;
  const uint8_t *data;
  size_t size;
};

template <> struct microfmt::address_space_traits<physical_space_tag> {
  using context_type = physical_space_context;

  static bool read_bytes(const void *opaque, uintptr_t address,
                         void *destination, size_t size) noexcept {
    if (!opaque || !destination)
      return false;
    const auto &context =
        *static_cast<const physical_space_context *>(opaque);
    if (address < context.base)
      return false;
    const uintptr_t offset = address - context.base;
    if (offset > context.size || size > context.size - offset)
      return false;
    MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;
    std::memcpy(destination, context.data + offset, size);
    MICROFMT_END_UNSAFE_BUFFER_USAGE;
    return true;
  }

  static bool read_string(const void *, uintptr_t, char *, size_t, size_t &,
                          bool &) noexcept {
    return false;
  }
};

namespace {

struct test_layout_state {
  uintptr_t physical_mask;
};

bool describe_test_level(const void *, size_t level,
                         microfmt::remote_page_table_level &description)
    noexcept {
  if (level == 0) {
    description = {14, 2, 8};
    return true;
  }
  if (level == 1) {
    description = {12, 2, 8};
    return true;
  }
  return false;
}

bool decode_test_entry(
    const void *opaque, size_t, uint64_t raw,
    microfmt::remote_page_table_decoded_entry &entry) noexcept {
  if (!opaque || (raw & 1U) == 0)
    return true;
  const auto &state = *static_cast<const test_layout_state *>(opaque);
  entry.output_address =
      static_cast<uintptr_t>(raw) & state.physical_mask;
  if ((raw & 2U) != 0) {
    entry.kind = microfmt::remote_page_table_entry_kind::leaf;
    entry.page_size = 4096;
    entry.readable = true;
    entry.writable = (raw & 4U) != 0;
    entry.user_accessible = (raw & 8U) != 0;
  } else {
    entry.kind = microfmt::remote_page_table_entry_kind::next_table;
  }
  return true;
}

void store_u64(uint8_t *memory, size_t offset, uint64_t value) {
  MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;
  std::memcpy(memory + offset, &value, sizeof(value));
  MICROFMT_END_UNSAFE_BUFFER_USAGE;
}

TEST(RemotePageTableWalker, WalksPhysicalTablesAndRecordsTrace) {
  constexpr uintptr_t physical_base = 0x1000;
  uint8_t memory[0x3000]{};
  constexpr uintptr_t root = 0x1000;
  constexpr uintptr_t next = 0x2000;
  constexpr uintptr_t virtual_address = 0x6123;
  constexpr uintptr_t physical_page = 0x3000;

  store_u64(memory, 8, next | 1U);
  store_u64(memory, 0x1000 + 16, physical_page | 1U | 2U | 4U | 8U);

  const physical_space_context space_context{
      physical_base, memory, sizeof(memory)};
  const microfmt::address_space_ref physical_space(physical_space_tag{},
                                                    space_context);
  const test_layout_state layout_state{~uintptr_t{0xfff}};
  const microfmt::remote_page_table_callbacks callbacks{
      2, describe_test_level, decode_test_entry};
  const microfmt::remote_page_table_layout_ref layout(layout_state,
                                                       callbacks);
  const microfmt::remote_page_table_walker walker(physical_space, layout);
  microfmt::remote_page_table_walk_step steps[2]{};
  microfmt::remote_page_table_walk_trace trace(steps);

  auto result = walker.walk(root, virtual_address, trace);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->physical_address, physical_page + 0x123);
  EXPECT_EQ(result->page_size, 4096U);
  EXPECT_TRUE(result->attributes.readable);
  EXPECT_TRUE(result->attributes.writable);
  EXPECT_TRUE(result->attributes.user_accessible);
  ASSERT_EQ(trace.size(), 2U);
  EXPECT_FALSE(trace.truncated());
  const auto traced_steps = trace.steps();
  EXPECT_EQ(traced_steps[0].table_address, root);
  EXPECT_EQ(traced_steps[0].decoded.kind,
            microfmt::remote_page_table_entry_kind::next_table);
  EXPECT_EQ(traced_steps[1].table_address, next);
  EXPECT_EQ(traced_steps[1].decoded.kind,
            microfmt::remote_page_table_entry_kind::leaf);
}

TEST(RemotePageTableWalker, KeepsBoundedTraceAndReportsInvalidEntries) {
  constexpr uintptr_t physical_base = 0x1000;
  uint8_t memory[0x2000]{};
  store_u64(memory, 8, 0x2000 | 1U);

  const physical_space_context space_context{
      physical_base, memory, sizeof(memory)};
  const microfmt::address_space_ref physical_space(physical_space_tag{},
                                                    space_context);
  const test_layout_state layout_state{~uintptr_t{0xfff}};
  const microfmt::remote_page_table_layout_ref layout(
      layout_state,
      {2, describe_test_level, decode_test_entry});
  const microfmt::remote_page_table_walker walker(physical_space, layout);
  microfmt::remote_page_table_walk_step steps[1]{};
  microfmt::remote_page_table_walk_trace trace(steps);

  auto result = walker.walk(0x1000, 0x6123, trace);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(),
            microfmt::remote_page_table_walk_error::invalid_entry);
  EXPECT_EQ(trace.size(), 1U);
  EXPECT_TRUE(trace.truncated());
}

TEST(RemotePageTableWalker, ReportsInvalidConfigurationAndReadFailures) {
  microfmt::remote_page_table_walk_step steps[2]{};
  microfmt::remote_page_table_walk_trace trace(steps);
  const microfmt::remote_page_table_walker empty({}, {});
  auto invalid_space = empty.walk(0x1000, 0, trace);
  ASSERT_FALSE(invalid_space);
  EXPECT_EQ(
      invalid_space.error(),
      microfmt::remote_page_table_walk_error::invalid_physical_space);

  uint8_t memory[8]{};
  const physical_space_context space_context{0x1000, memory, sizeof(memory)};
  const microfmt::address_space_ref physical_space(physical_space_tag{},
                                                    space_context);
  const test_layout_state layout_state{~uintptr_t{0xfff}};
  const microfmt::remote_page_table_layout_ref layout(
      layout_state,
      {2, describe_test_level, decode_test_entry});
  const microfmt::remote_page_table_walker walker(physical_space, layout);
  auto read_failure = walker.walk(0x2000, 0, trace);
  ASSERT_FALSE(read_failure);
  EXPECT_EQ(read_failure.error(),
            microfmt::remote_page_table_walk_error::read_failed);
}

} // namespace
