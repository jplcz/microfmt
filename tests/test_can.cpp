// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <cstdint>

#include <microfmt/formatters/can.hpp>

TEST(CanFormatterTest, FormatsStandardFrameAndMasksIdentifier) {
  const uint8_t payload[] = {0x10, 0x02, 0xFF};

  const auto formatted = microfmt::format<128>(
      "{}", microfmt::can_frame(0x0923, payload));

  EXPECT_EQ(formatted.view(), "CAN [0x123] DLC=3 DATA: 10 02 FF");
}

TEST(CanFormatterTest, FormatsExtendedRemoteFrame) {
  const uint8_t payload[] = {0, 0, 0, 0};

  const auto formatted = microfmt::format<128>(
      "{}", microfmt::can_extended(0x18DAF110, microfmt::span(payload), true));

  EXPECT_EQ(formatted.view(), "CAN [0x18DAF110] EXT RTR (DLC=4)");
}

TEST(CanFormatterTest, FormatsCanFdCandumpFlagsAndLowercase) {
  const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
  const auto frame =
      microfmt::can_fd(0x18DAF110, microfmt::span(payload), true, true, true);

  EXPECT_EQ(microfmt::format<128>("{}", frame).view(),
            "CAN-FD [0x18DAF110] EXT BRS ESI DLC=4 DATA: DE AD BE EF");
  EXPECT_EQ(microfmt::format<128>("{:c}", frame).view(),
            "18DAF110##3DEADBEEF");
  EXPECT_EQ(microfmt::format<128>("{:cx}", frame).view(),
            "18daf110##3deadbeef");
}
