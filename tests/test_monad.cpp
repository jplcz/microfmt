// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <optional>

#include <gtest/gtest.h>

#include <microfmt/formatters/monad.hpp>
#include <microfmt/microfmt.hpp>

TEST(MonadTest, FormatsOptionalValuesAndForwardsSpecs) {
  microfmt::buffer_sink<32> buffer;

  microfmt::format_to(buffer.as_sink(), "{:04x}", std::optional<int>{42});
  EXPECT_EQ(buffer.view(), "Some(002a)");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{}", std::optional<int>{});
  EXPECT_EQ(buffer.view(), "None");
}

TEST(MonadTest, FormatsMicrofmtExpectedValueAndError) {
  microfmt::buffer_sink<32> buffer;

  microfmt::format_to(buffer.as_sink(), "{:04x}",
                      microfmt::expected<int, int>{42});
  EXPECT_EQ(buffer.view(), "Ok(002a)");

  buffer.reset();
  microfmt::format_to(
      buffer.as_sink(), "{}",
      microfmt::expected<int, int>{microfmt::unexpected{7}});
  EXPECT_EQ(buffer.view(), "Err(7)");
}

TEST(MonadTest, FormatsVoidMicrofmtExpected) {
  microfmt::buffer_sink<32> buffer;

  microfmt::format_to(buffer.as_sink(), "{}",
                      microfmt::expected<void, int>{});
  EXPECT_EQ(buffer.view(), "Ok()");

  buffer.reset();
  microfmt::format_to(
      buffer.as_sink(), "{}",
      microfmt::expected<void, int>{microfmt::unexpected{7}});
  EXPECT_EQ(buffer.view(), "Err(7)");
}

TEST(MonadTest, TransformsMicrofmtExpected) {
  const microfmt::expected<int, int> value{21};
  const auto transformed = value.transform([](int v) noexcept { return v * 2; });

  ASSERT_TRUE(transformed.has_value());
  EXPECT_EQ(transformed.value(), 42);
}

#if MICROFMT_HAS_STD_EXPECTED
TEST(MonadTest, FormatsExpectedValueAndError) {
  microfmt::buffer_sink<32> buffer;

  microfmt::format_to(buffer.as_sink(), "{:04x}", std::expected<int, int>{42});
  EXPECT_EQ(buffer.view(), "Ok(002a)");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{}",
                      std::expected<int, int>{std::unexpect, 7});
  EXPECT_EQ(buffer.view(), "Err(7)");
}
#endif
