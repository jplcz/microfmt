// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <gtest/gtest.h>
#include <microfmt/formatters/escaped.hpp>
#include <microfmt/microfmt.hpp>

TEST(EscapedTest, PrintableAscii) {
  microfmt::buffer_sink<64> buf;

  microfmt::format_to(buf.as_sink(), "{}", microfmt::escaped("Hello, World!"));
  EXPECT_EQ(buf.view(), "\"Hello, World!\"");

  // Without outer quotes
  buf.reset();
  microfmt::format_to(buf.as_sink(), "{}",
                      microfmt::escaped("Hello, World!", /*quote=*/false));
  EXPECT_EQ(buf.view(), "Hello, World!");
}

TEST(EscapedTest, ControlCharacters) {
  microfmt::buffer_sink<64> buf;

  const microfmt::string_view at_cmd{"AT+CGATT=1\r\n\0OK\t\x1B",
                                     sizeof("AT+CGATT=1\r\n\0OK\t\x1B") - 1};
  microfmt::format_to(buf.as_sink(), "{}",
                      microfmt::escaped(at_cmd, /*quote=*/true));

  EXPECT_EQ(buf.view(), "\"AT+CGATT=1\\r\\n\\0OK\\t\\x1b\"");
}

TEST(EscapedTest, QuoteAndBackslashEscaping) {
  microfmt::buffer_sink<64> buf;

  // String with nested quotes and backslashes
  const microfmt::string_view payload = R"(key="val\1")";
  microfmt::format_to(buf.as_sink(), "{}", microfmt::escaped(payload));
  EXPECT_EQ(buf.view(), "\"key=\\\"val\\\\1\\\"\"");
}

TEST(EscapedTest, RawBinaryBufferWithNulls) {
  microfmt::buffer_sink<64> buf;

  const uint8_t binary_packet[] = {0x00, 0x01, 0xFF, 0x41, 0x0A, 0x7E};
  microfmt::format_to(
      buf.as_sink(), "Packet: {}",
      microfmt::escaped(microfmt::span(binary_packet), /*quote=*/false));

  EXPECT_EQ(buf.view(), "Packet: \\0\\x01\\xffA\\n~");
}

TEST(EscapedTest, EmptyInput) {
  microfmt::buffer_sink<64> buf;

  microfmt::format_to(buf.as_sink(), "{}", microfmt::escaped(""));
  EXPECT_EQ(buf.view(), "\"\"");

  buf.reset();
  microfmt::format_to(buf.as_sink(), "{}",
                      microfmt::escaped("", /*quote=*/false));
  EXPECT_EQ(buf.view(), "");
}
