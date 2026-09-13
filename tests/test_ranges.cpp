#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <microfmt/microfmt.hpp>
#include <microfmt/formatters/ranges.hpp>
#include <string_view>

TEST(JoinTest, EmptyRange) {
  microfmt::buffer_sink<64> buf;
  const int data[] = {1};

  // Empty iterator range
  microfmt::format_to(buf.as_sink(), "{}", microfmt::join(data, data));
  EXPECT_EQ(buf.view(), "");

  buf.reset();
  microfmt::span<const int> empty_span{};
  microfmt::format_to(buf.as_sink(), "[{}]", microfmt::join(empty_span));
  EXPECT_EQ(buf.view(), "[]");
}

TEST(JoinTest, SingleElement) {
  microfmt::buffer_sink<64> buf;
  const int data[] = {42};

  microfmt::format_to(buf.as_sink(), "{}", microfmt::join(data));
  EXPECT_EQ(buf.view(), "42");
}

TEST(JoinTest, CArrayDefaultDelimiter) {
  microfmt::buffer_sink<64> buf;
  const int32_t ports[] = {80, 443, 8080};

  microfmt::format_to(buf.as_sink(), "Ports: [{}]", microfmt::join(ports));
  EXPECT_EQ(buf.view(), "Ports: [80, 443, 8080]");
}

TEST(JoinTest, CustomDelimiter) {
  microfmt::buffer_sink<64> buf;
  const uint8_t mac[] = {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E};

  // Custom single char and multi-char delimiters
  microfmt::format_to(buf.as_sink(), "{}", microfmt::join(mac, ":"));
  EXPECT_EQ(buf.view(), "0:26:43:60:77:94");

  buf.reset();
  const std::string_view tags[] = {"sensor", "temp", "ch1"};
  microfmt::format_to(buf.as_sink(), "{}", microfmt::join(tags, " -> "));
  EXPECT_EQ(buf.view(), "sensor -> temp -> ch1");
}

TEST(JoinTest, SpanAndStdArray) {
  microfmt::buffer_sink<128> buf;
  std::array<int16_t, 4> readings{-10, 0, 15, 22};

  microfmt::span<const int16_t> sp(readings.data(), readings.size());
  microfmt::format_to(buf.as_sink(), "T: {}", microfmt::join(sp, " | "));
  EXPECT_EQ(buf.view(), "T: -10 | 0 | 15 | 22");
}

TEST(JoinTest, DirectFormatEmbedding) {
  const uint32_t channel_mask[] = {1, 2, 4, 8};
  auto res = microfmt::format<128>("Channels active: ({})",
                                   microfmt::join(channel_mask, ", "));

  EXPECT_EQ(res.view(), "Channels active: (1, 2, 4, 8)");
}
