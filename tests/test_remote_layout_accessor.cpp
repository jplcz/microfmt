// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/inspector/remote_layout_accessor.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

namespace {

template <typename T> uintptr_t address_of(const T &object) noexcept {
  return reinterpret_cast<uintptr_t>(&object);
}

microfmt::address_space_ref local_space() {
  return microfmt::address_space_ref(microfmt::local_space_tag{});
}

struct layout_object {
  uint32_t narrow;
  uint64_t wide;
};

TEST(RemoteLayoutAccessor, ReadsTypedFixedOffsets) {
  const layout_object object{17, 29};
  const auto narrow_query =
      microfmt::make_remote_offset_query<size_t, uint32_t>(
          offsetof(layout_object, narrow));
  const auto wide_query =
      microfmt::make_remote_offset_query<uint64_t>(
          offsetof(layout_object, wide));

  size_t narrow = 0;
  uint64_t wide = 0;
  EXPECT_TRUE(narrow_query(local_space(), address_of(object), narrow));
  EXPECT_TRUE(wide_query(local_space(), address_of(object), wide));
  EXPECT_EQ(narrow, 17U);
  EXPECT_EQ(wide, 29U);
}

TEST(RemoteLayoutAccessor, SupportsNegativeOffsets) {
  const layout_object object{17, 29};
  const auto query =
      microfmt::make_remote_offset_query<uint32_t>(
          -static_cast<ptrdiff_t>(offsetof(layout_object, wide)));

  uint32_t value = 0;
  EXPECT_TRUE(query(local_space(), address_of(object.wide), value));
  EXPECT_EQ(value, 17U);
}

TEST(RemoteLayoutAccessor, RejectsNarrowingOverflowAndReadFailures) {
  const layout_object object{17, UINT64_MAX};
  const auto query =
      microfmt::make_remote_offset_query<uint32_t, uint64_t>(
          offsetof(layout_object, wide));

  uint32_t value = 0;
  EXPECT_FALSE(query(local_space(), address_of(object), value));
  EXPECT_FALSE(query(microfmt::address_space_ref{}, address_of(object),
                     value));
}

TEST(RemoteLayoutAccessor, RetainsCallbacksAndForwardsQueryInputs) {
  struct reader {
    ptrdiff_t offset;
    uint64_t bias;

    bool operator()(microfmt::address_space_ref space,
                    uintptr_t object_address, uint64_t &result,
                    uint64_t multiplier) const noexcept {
      uint64_t stored = 0;
      if (!space.read(object_address + static_cast<uintptr_t>(offset),
                      stored))
        return false;
      result = stored * multiplier + bias;
      return true;
    }
  };

  const layout_object object{17, 29};
  const auto query = microfmt::make_remote_layout_query<uint64_t>(
      reader{offsetof(layout_object, wide), 3});

  uint64_t result = 0;
  EXPECT_TRUE(
      query(local_space(), address_of(object), result, uint64_t{2}));
  EXPECT_EQ(result, 61U);
}

} // namespace
