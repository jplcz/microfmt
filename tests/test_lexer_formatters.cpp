// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause


#include <gtest/gtest.h>

#include <reloco/string.hpp>

#include <microfmt/microfmt.hpp>
#include <microfmt/parsing/lexer_formatters.hpp>
#include <microfmt/parsing/lexer_rules_presets.hpp>

TEST(LexerFormattersTest, FormatsKindAndToken) {
  microfmt::buffer_sink<128> buffer;
  microfmt::lexer lx{"x\t1", microfmt::rules::c_like};
  microfmt::format_to(buffer.as_sink(), "{} | {}", microfmt::token_kind::keyword, lx.next());
  EXPECT_EQ(buffer.view(), "keyword | identifier \"x\" @1:1");
}

TEST(LexerFormattersTest, HighlightWrapsTokensInAnsiAndPreservesText) {
  constexpr microfmt::string_view source = "if x // c\n{ \"s\" }";
  microfmt::buffer_sink<512> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::highlight(source, microfmt::rules::c_like));

  const microfmt::string_view rendered = buffer.view();
  EXPECT_NE(rendered.find("\x1b["), microfmt::string_view::npos);

  // Strip CSI sequences; what remains must equal the input exactly.
  reloco::string stripped;
  for (size_t i = 0; i < rendered.size(); ++i) {
    if (rendered[i] == '\x1b') {
      while (i < rendered.size() && rendered[i] != 'm') {
        ++i;
      }
    } else {
      ASSERT_TRUE(stripped.try_push_back(rendered[i]));
    }
  }
  EXPECT_EQ(stripped.view(), source);
}

TEST(LexerFormattersTest, DefaultStyleLeavesTextUnstyled) {
  microfmt::highlight_theme plain;
  plain.keyword = {};
  plain.number = {};
  plain.string = {};
  plain.comment = {};
  plain.op = {};
  plain.error = {};

  microfmt::buffer_sink<128> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::highlight("if 1 + 2", microfmt::rules::c_like, plain));
  EXPECT_EQ(buffer.view(), "if 1 + 2");
}
