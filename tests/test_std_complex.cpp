// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <complex>

#include <gtest/gtest.h>

#include <microfmt/formatters/std_complex.hpp>
#include <microfmt/microfmt.hpp>

TEST(StdComplexTest, FormatsComplexAndForwardsSpec) {
  microfmt::buffer_sink<64> buffer;
  microfmt::format_to(buffer.as_sink(), "{:.2f}", std::complex<double>(1.5, 2.25));
  EXPECT_EQ(buffer.view(), "(1.50+2.25i)");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{:.1f}", std::complex<float>(1.0f, -3.0f));
  EXPECT_EQ(buffer.view(), "(1.0-3.0i)");
}
