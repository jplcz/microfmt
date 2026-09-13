#include <gtest/gtest.h>

#include <microfmt/formatters/tuple.hpp>
#include <microfmt/microfmt.hpp>
#include <string>
#include <tuple>
#include <utility>

TEST(TupleTest, FormatsStandardTupleAndPair) {
  microfmt::buffer_sink<128> output;
  const auto values = std::make_tuple(7, "uart", true);

  microfmt::format_to(output.as_sink(), "{}", values);
  EXPECT_EQ(output.view(), "(7, uart, true)");

  output.reset();
  microfmt::format_to(output.as_sink(), "{}", std::make_pair(3, 42));
  EXPECT_EQ(output.view(), "(3, 42)");
}

TEST(TupleTest, DelimiterStylesAndElementSpecifiers) {
  microfmt::buffer_sink<128> output;
  const std::tuple<uint16_t, uint16_t, uint16_t> registers{
      0x00A1, 0x000F, 0xBEEF};

  microfmt::format_to(output.as_sink(), "{:b04X}", registers);
  EXPECT_EQ(output.view(), "[00A1, 000F, BEEF]");

  output.reset();
  microfmt::format_to(output.as_sink(), "{:n04x}", registers);
  EXPECT_EQ(output.view(), "00a1, 000f, beef");

  output.reset();
  microfmt::format_to(output.as_sink(), "{:c}", std::make_pair("rx", 18));
  EXPECT_EQ(output.view(), "{rx, 18}");
}

TEST(TupleTest, HandlesEmptyTuple) {
  microfmt::buffer_sink<32> output;
  const std::tuple<> empty;

  microfmt::format_to(output.as_sink(), "{}", empty);
  EXPECT_EQ(output.view(), "()");

  output.reset();
  microfmt::format_to(output.as_sink(), "{:b}", empty);
  EXPECT_EQ(output.view(), "[]");
}
