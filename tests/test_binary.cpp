#include <cstdint>
#include <gtest/gtest.h>
#include <microfmt/binary.hpp>
#include <microfmt/microfmt.hpp>

TEST(BinaryViewTest, TypeInferredBitWidth) {
  microfmt::buffer_sink<64> buf;

  // uint8_t -> 8 bits
  uint8_t val8 = 0x2A;
  microfmt::format_to(buf.as_sink(), "{}", microfmt::bin(val8));
  EXPECT_EQ(buf.view(), "00101010");

  // uint16_t -> 16 bits
  buf.reset();
  uint16_t val16 = 0x00FF;
  microfmt::format_to(buf.as_sink(), "{}", microfmt::bin(val16));
  EXPECT_EQ(buf.view(), "0000000011111111");
}

TEST(BinaryViewTest, ExplicitBitWidth) {
  microfmt::buffer_sink<64> buf;

  // Render lower 4 bits only
  microfmt::format_to(buf.as_sink(), "{}", microfmt::bin<4>(0x2A));
  EXPECT_EQ(buf.view(), "1010");

  // Render lower 12 bits of a 32-bit int
  buf.reset();
  microfmt::format_to(buf.as_sink(), "{}", microfmt::bin<12>(0x0ABC));
  EXPECT_EQ(buf.view(), "101010111100");
}

TEST(BinaryViewTest, PrefixesAndGrouping) {
  microfmt::buffer_sink<64> buf;

  // Prefixed 0b
  microfmt::format_to(buf.as_sink(), "{}", microfmt::bin_prefixed(uint8_t{5}));
  EXPECT_EQ(buf.view(), "0b00000101");

  // Nibble grouped
  buf.reset();
  microfmt::format_to(buf.as_sink(), "{}",
                      microfmt::bin_grouped(uint16_t{0xA55A}));
  EXPECT_EQ(buf.view(), "1010_0101_0101_1010");

  // Specifier flags override ({:#_})
  buf.reset();
  microfmt::format_to(buf.as_sink(), "{:#_}", microfmt::bin(uint8_t{0xA5}));
  EXPECT_EQ(buf.view(), "0b1010_0101");
}
