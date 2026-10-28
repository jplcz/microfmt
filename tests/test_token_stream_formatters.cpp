// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/microfmt.hpp>
#include <microfmt/parsing/lexer_rules_presets.hpp>
#include <microfmt/parsing/token_stream_formatters.hpp>

using microfmt::parse_error;
using microfmt::string_view;
using microfmt::token_stream;

namespace {

// Advances `ts` to the first token whose spelling is `text`.
void skip_to(token_stream &ts, string_view text) {
  while (!ts.at_end() && ts.peek().text != text) {
    ts.next();
  }
}

} // namespace

TEST(ErrorReportTest, CaretPointsAtTheOffendingColumn) {
  constexpr string_view source = "{\"a\": [1, 2,, 3]}";
  token_stream ts{source, microfmt::rules::json};
  skip_to(ts, ",");
  ts.next();
  skip_to(ts, ",");
  ts.next();
  const parse_error err = ts.error_here("expected a value");

  microfmt::buffer_sink<256> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::report(err, source));
  EXPECT_EQ(buffer.view(), "expected a value (1:13)\n{\"a\": [1, 2,, 3]}\n            ^");
}

TEST(ErrorReportTest, ShowsOnlyTheFailingLineAndStripsCr) {
  constexpr string_view source = "a\r\n  @\r\nb";
  token_stream ts{source, microfmt::rules::json};
  ts.next();
  const parse_error err = ts.error_here("bad byte");

  microfmt::buffer_sink<256> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::report(err, source));
  EXPECT_EQ(buffer.view(), "bad byte (2:3)\n  @\n  ^");
}

TEST(ErrorReportTest, EndTokenAndStaleOffsetStayInBounds) {
  constexpr string_view source = "[1,";
  token_stream ts{source, microfmt::rules::json};
  while (!ts.at_end()) {
    ts.next();
  }
  parse_error err = ts.error_here("unexpected end");
  microfmt::buffer_sink<256> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::report(err, source));
  EXPECT_EQ(buffer.view(), "unexpected end (1:4)\n[1,\n   ^");

  err.at.offset = 1000; // error built for a longer text than the one passed to report()
  microfmt::buffer_sink<256> clamped;
  microfmt::format_to(clamped.as_sink(), "{}", microfmt::report(err, source));
  EXPECT_EQ(clamped.view(), "unexpected end (1:4)\n[1,\n   ^");
}
