// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/formatters/math.hpp>
#include <microfmt/microfmt.hpp>

TEST(MathTest, FormatsVectorsAndForwardsElementSpecs) {
  const int values[] = {1, 2, 3};
  microfmt::buffer_sink<32> buffer;

  microfmt::format_to(buffer.as_sink(), "{}", microfmt::vec(values));
  EXPECT_EQ(buffer.view(), "<1, 2, 3>");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{:04x}", microfmt::vec3(1, 2, 3));
  EXPECT_EQ(buffer.view(), "<0001, 0002, 0003>");
}

TEST(MathTest, Vec3OwnsValuesFromSeparateArguments) {
  int x = 1;
  int y = 2;
  int z = 3;
  const auto value = microfmt::vec3(x, y, z);
  x = 4;
  y = 5;
  z = 6;

  const auto rendered = microfmt::format<32>("{}", value);
  EXPECT_EQ(rendered.view(), "<1, 2, 3>");
}

TEST(MathTest, FormatsRowMajorMatrices) {
  const int values[] = {1, 2, 3, 4, 5, 6};

  const auto rendered =
      microfmt::format<64>("{}", microfmt::mat<int, 2, 3>(values));

  EXPECT_EQ(rendered.view(), "[\n  [1, 2, 3]\n  [4, 5, 6]\n]");
}
