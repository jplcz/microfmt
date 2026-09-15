// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory_resource>
#include <string>

#include <microfmt/sinks/pmr_sink.hpp>

TEST(PmrSinkTest, FormatsStringAndVectorWithProvidedResource) {
  std::array<std::byte, 512> storage{};
  std::pmr::monotonic_buffer_resource resource(storage.data(), storage.size());

  const auto text = microfmt::pmr::format(&resource, "value={}", 42);
  const auto bytes =
      microfmt::pmr::format_vector(&resource, "id={:04x}", 0x2a);

  EXPECT_EQ(text.get_allocator().resource(), &resource);
  EXPECT_EQ(bytes.get_allocator().resource(), &resource);
  EXPECT_EQ(text, "value=42");
  EXPECT_EQ(std::string(bytes.begin(), bytes.end()), "id=002a");
}

TEST(PmrSinkTest, ArenaSinkGrowsAndPreservesOutput) {
  std::array<std::byte, 512> storage{};
  std::pmr::monotonic_buffer_resource resource(storage.data(), storage.size());
  microfmt::pmr::arena_sink output(&resource, 4);

  microfmt::format_to(output.as_sink(), "reading={:04x}", 0x2a);

  EXPECT_EQ(output.view(), "reading=002a");
}
