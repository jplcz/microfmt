// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <string>

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

TEST(ErrorReportTest, NotePrintsItsOwnSnippetAndCaret) {
  constexpr string_view source = "[\n  1,\n  2";
  token_stream ts{source, microfmt::rules::json};
  const microfmt::token open = ts.next(); // '[' on line 1
  while (!ts.at_end()) {
    ts.next();
  }
  const parse_error err = microfmt::with_note(ts.error_here("expected ']'"), "list opened here", open);

  microfmt::buffer_sink<256> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::report(err, source));
  EXPECT_EQ(buffer.view(), "expected ']' (3:4)\n  2\n   ^\nnote: list opened here (1:1)\n[\n^");
}

TEST(ErrorReportTest, WithNoteLeavesTheOriginalAndOtherFieldsUntouched) {
  token_stream ts{"a b", microfmt::rules::json};
  const parse_error original = ts.error_here("bad");
  const parse_error noted = microfmt::with_note(original, "see here", ts.peek());
  EXPECT_TRUE(original.note.empty());
  EXPECT_EQ(noted.message, "bad");
  EXPECT_EQ(noted.note, "see here");
  EXPECT_EQ(noted.note_at.text, "a");
}

namespace {

struct text_reader {
  string_view data;
  size_t pos{0};
  size_t operator()(reloco::span<char> out) noexcept {
    size_t n = data.size() - pos;
    n = n < out.size() ? n : out.size();
    for (size_t i = 0; i < n; ++i) {
      out[i] = data[pos + i];
    }
    pos += n;
    return n;
  }
};

} // namespace

TEST(ErrorReportTest, StreamedErrorsUseTheWindowBaseAndDropOldSnippets) {
  std::string text(200, ' ');
  text += "oops here";
  text_reader reader{text};
  microfmt::stream_window<256> window{reader};
  token_stream ts{window.ref(), microfmt::lexer_rules{}};
  const microfmt::token t = ts.next();
  const parse_error err{"bad name", t};

  microfmt::buffer_sink<512> live;
  microfmt::format_to(live.as_sink(), "{}", microfmt::report(err, ts.source(), ts.source_base()));
  EXPECT_NE(live.view().find("oops"), string_view::npos);

  ts.next();
  ts.discard_consumed();
  microfmt::buffer_sink<512> old;
  microfmt::format_to(old.as_sink(), "{}", microfmt::report(err, ts.source(), ts.source_base()));
  EXPECT_NE(old.view().find("bad name"), string_view::npos);
  EXPECT_EQ(old.view().find("oops"), string_view::npos);

  microfmt::buffer_sink<512> stored;
  microfmt::format_to(stored.as_sink(), "{}", microfmt::report(microfmt::store(err), ts.source(), ts.source_base()));
  EXPECT_NE(stored.view().find("bad name"), string_view::npos);
}
