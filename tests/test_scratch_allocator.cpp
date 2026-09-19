// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/scratch_allocator.hpp>

#include <cstddef>
#include <cstdint>

namespace {

struct alignas(16) aligned_value {
  explicit aligned_value(int initial) noexcept : value(initial) {}

  int value;
};

static_assert(std::is_trivially_destructible_v<aligned_value>);

TEST(ScratchAllocator, AllocatesAlignedObjectArrays) {
  alignas(std::max_align_t) std::byte storage[64]{};
  microfmt::scratch_allocator allocator(storage);

  auto *values = allocator.allocate<uint32_t>(4);

  ASSERT_NE(values, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(values) % alignof(uint32_t), 0U);
  EXPECT_EQ(allocator.size(), sizeof(uint32_t) * 4);
  EXPECT_EQ(allocator.available(), sizeof(storage) - allocator.size());
}

TEST(ScratchAllocator, AlignsWithinUnalignedBuffers) {
  alignas(std::max_align_t) std::byte storage[64]{};
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  microfmt::scratch_allocator allocator(
      microfmt::span<std::byte>(storage + 1, sizeof(storage) - 1));
  RELOCO_END_UNSAFE_BUFFER_USAGE;

  auto *value = allocator.create<aligned_value>(42);

  ASSERT_NE(value, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(value) % alignof(aligned_value), 0U);
  EXPECT_EQ(value->value, 42);
  EXPECT_GE(allocator.size(), sizeof(aligned_value));
}

TEST(ScratchAllocator, RejectsInvalidOrExhaustedAllocations) {
  alignas(uint32_t) std::byte storage[sizeof(uint32_t)]{};
  microfmt::scratch_allocator allocator(storage);

  EXPECT_EQ(allocator.allocate<uint32_t>(0), nullptr);
  ASSERT_NE(allocator.allocate<uint32_t>(), nullptr);
  const auto used = allocator.size();
  EXPECT_EQ(allocator.allocate<uint8_t>(), nullptr);
  EXPECT_EQ(allocator.size(), used);
}

TEST(ScratchAllocator, PartitionsRemainingStorage) {
  alignas(std::max_align_t) std::byte storage[64]{};
  microfmt::scratch_allocator allocator(storage);
  ASSERT_NE(allocator.allocate<uint32_t>(), nullptr);

  const auto parent_size = allocator.size();
  auto child = allocator.rest();

  EXPECT_EQ(child.capacity(), allocator.available());
  ASSERT_NE(child.allocate<uint64_t>(), nullptr);
  EXPECT_EQ(allocator.size(), parent_size);
}

TEST(ScratchAllocator, ResetReusesBackingStorage) {
  alignas(std::max_align_t) std::byte storage[32]{};
  microfmt::scratch_allocator allocator(storage);

  auto *first = allocator.allocate<uint64_t>();
  ASSERT_NE(first, nullptr);

  allocator.reset();
  auto *second = allocator.allocate<uint64_t>();

  EXPECT_EQ(second, first);
}

TEST(ScratchAllocator, AcceptsCharacterStorage) {
  alignas(std::max_align_t) char storage[32]{};
  microfmt::scratch_allocator allocator(
      microfmt::span<char>(storage, sizeof(storage)));

  auto *value = allocator.create<aligned_value>(7);

  ASSERT_NE(value, nullptr);
  EXPECT_EQ(value->value, 7);
}

} // namespace
