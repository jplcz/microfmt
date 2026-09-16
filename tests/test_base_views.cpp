// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>

#include <gtest/gtest.h>

#include <microfmt/formatters/base_views.hpp>
#include <microfmt/microfmt.hpp>

TEST(BaseViewsTest, EncodesBase64WithPadding) {
  const uint8_t one[] = {'M'};
  const uint8_t two[] = {'M', 'a'};
  const uint8_t three[] = {'M', 'a', 'n'};
  microfmt::buffer_sink<16> buffer;

  microfmt::format_to(buffer.as_sink(), "{}", microfmt::base64(one));
  EXPECT_EQ(buffer.view(), "TQ==");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::base64(two));
  EXPECT_EQ(buffer.view(), "TWE=");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::base64(three));
  EXPECT_EQ(buffer.view(), "TWFu");
}

TEST(BaseViewsTest, GroupsBinaryWithCustomSeparators) {
  microfmt::buffer_sink<32> buffer;

  microfmt::format_to(buffer.as_sink(), "{}",
                      microfmt::bin_grouped(uint8_t{0xa5}, 2, ':'));
  EXPECT_EQ(buffer.view(), "0b10:10:01:01");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{}",
                      microfmt::bin_grouped(uint8_t{0xa5}, 0));
  EXPECT_EQ(buffer.view(), "0b10100101");
}
