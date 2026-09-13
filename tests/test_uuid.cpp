#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <microfmt/microfmt.hpp>
#include <microfmt/formatters/uuid.hpp>
#include <string_view>

TEST(UuidTest, CanonicalFormat) {
  microfmt::buffer_sink<64> buf;

  // Standard RFC 4122 test UUID: 6ba7b810-9dad-11d1-80b4-00c04fd430c8
  const uint8_t raw_uuid[16] = {0x6b, 0xa7, 0xb8, 0x10, 0x9d, 0xad, 0x11, 0xd1,
                                0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8};

  microfmt::format_to(buf.as_sink(), "{}", microfmt::uuid(raw_uuid));
  EXPECT_EQ(buf.view(), "6ba7b810-9dad-11d1-80b4-00c04fd430c8");
}

TEST(UuidTest, UppercaseAndBracedSpecs) {
  microfmt::buffer_sink<64> buf;

  const std::array<uint8_t, 16> raw_uuid = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
                                            0xcd, 0xef, 0xfe, 0xdc, 0xba, 0x98,
                                            0x76, 0x54, 0x32, 0x10};

  // Uppercase via {:X}
  microfmt::format_to(buf.as_sink(), "{:X}", microfmt::uuid(raw_uuid));
  EXPECT_EQ(buf.view(), "01234567-89AB-CDEF-FEDC-BA9876543210");

  // Braced via {:#}
  buf.reset();
  microfmt::format_to(buf.as_sink(), "{:#}", microfmt::uuid(raw_uuid));
  EXPECT_EQ(buf.view(), "{01234567-89ab-cdef-fedc-ba9876543210}");

  // Combined {:#X}
  buf.reset();
  microfmt::format_to(buf.as_sink(), "{:#X}", microfmt::uuid(raw_uuid));
  EXPECT_EQ(buf.view(), "{01234567-89AB-CDEF-FEDC-BA9876543210}");
}

TEST(UuidTest, NilOrInvalidLength) {
  microfmt::buffer_sink<64> buf;

  // Partial byte span -> fall back to nil UUID string
  const uint8_t short_bytes[8] = {};
  microfmt::format_to(buf.as_sink(), "{}",
                      microfmt::uuid(microfmt::span(short_bytes, 8)));
  EXPECT_EQ(buf.view(), "00000000-0000-0000-0000-000000000000");
}