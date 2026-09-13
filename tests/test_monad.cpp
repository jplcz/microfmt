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
