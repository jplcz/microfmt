// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <microfmt/formatters/semver.hpp>
#include <microfmt/microfmt.hpp>

TEST(SemverTest, FormatsComponentsAndMetadata) {
  microfmt::buffer_sink<64> buffer;

  microfmt::format_to(buffer.as_sink(), "{}",
                      microfmt::version(1, 2, 3, "rc.1", "build.42"));

  EXPECT_EQ(buffer.view(), "1.2.3-rc.1+build.42");
}

TEST(SemverTest, AppliesPrefixAndCoreFormatSpecifiers) {
  microfmt::buffer_sink<64> buffer;
  const auto version = microfmt::version(2, 0, 1, "beta", "20260913");

  microfmt::format_to(buffer.as_sink(), "{:#}", version);
  EXPECT_EQ(buffer.view(), "v2.0.1-beta+20260913");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{:c}", version);
  EXPECT_EQ(buffer.view(), "2.0.1");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{:Vc}", version);
  EXPECT_EQ(buffer.view(), "v2.0.1");
}

TEST(SemverTest, FormatsPackedVersions) {
  microfmt::buffer_sink<32> buffer;

  microfmt::format_to(buffer.as_sink(), "{}",
                      microfmt::from_packed32(0x0102FEDCu));
  EXPECT_EQ(buffer.view(), "1.2.65244");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{:v}",
                      microfmt::from_packed24(0x0A0B0Cu));
  EXPECT_EQ(buffer.view(), "v10.11.12");
}
