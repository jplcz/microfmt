// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause


#include <gtest/gtest.h>

#include <reloco/array.hpp>
#include <reloco/string.hpp>
#include <reloco/vector.hpp>

#include <microfmt/parsing/lexer.hpp>
#include <microfmt/parsing/lexer_rules_presets.hpp>

using microfmt::lexer;
using microfmt::lexer_rules;
using microfmt::token;
using microfmt::token_kind;
using microfmt::string_view;

namespace {

reloco::vector<token> lex_all(string_view text, const lexer_rules &rules) {
  lexer lx{text, rules};
  reloco::vector<token> tokens;
  for (token t = lx.next(); t.kind != token_kind::end; t = lx.next()) {
    if (!tokens.try_push_back(t)) {
      break;
    }
    if (tokens.size() > text.size() + 1) {
      break; // guards against a non-terminating lexer
    }
  }
  return tokens;
}

constexpr microfmt::keyword_table kWords{"let", "if", "else"};
constexpr string_view kOps[] = {"=", "==", "=>", "+"};

lexer_rules simple_rules() {
  lexer_rules r;
  r.keywords = kWords;
  r.operators = kOps;
  r.line_comment = "//";
  r.block_comment_open = "/*";
  r.block_comment_close = "*/";
  return r;
}

} // namespace

TEST(LexerTest, KeywordTableSortsAnyOrderAndFindsWords) {
  constexpr microfmt::keyword_table table{"zeta", "alpha", "mid"};
  const microfmt::sorted_words words = table;
  EXPECT_EQ(words.size(), 3u);
  EXPECT_TRUE(words.contains("alpha"));
  EXPECT_TRUE(words.contains("mid"));
  EXPECT_TRUE(words.contains("zeta"));
  EXPECT_FALSE(words.contains("beta"));
  EXPECT_FALSE(words.contains(""));
}

TEST(LexerTest, SortedWordsFactoriesVerifyOrder) {
  static constexpr string_view sorted[] = {"a", "b", "c"};
  static constexpr string_view unsorted[] = {"b", "a"};
  static constexpr string_view duplicate[] = {"a", "a"};

  EXPECT_TRUE(microfmt::sorted_words::try_from_sorted(sorted).has_value());
  EXPECT_FALSE(microfmt::sorted_words::try_from_sorted(unsorted).has_value());
  EXPECT_FALSE(microfmt::sorted_words::try_from_sorted(duplicate).has_value());
  EXPECT_TRUE(microfmt::sorted_words::from_sorted(sorted).contains("b"));
}

TEST(LexerTest, LexesIdentifiersKeywordsNumbersAndOperators) {
  const auto tokens = lex_all("let x1 = 0x2A + 1_000 + 1.5e3 + .5", simple_rules());
  const reloco::array<token_kind, 10> expected{token_kind::keyword, token_kind::identifier, token_kind::op,
                                               token_kind::integer, token_kind::op,         token_kind::integer,
                                               token_kind::op,      token_kind::floating,   token_kind::op,
                                               token_kind::floating};
  ASSERT_EQ(tokens.size(), 10u);
  for (size_t i = 0; i < tokens.size(); ++i) {
    EXPECT_EQ(tokens[i].kind, expected[i]) << i;
  }
  EXPECT_EQ(tokens[3].text, "0x2A");
  EXPECT_EQ(tokens[5].text, "1_000");
  EXPECT_EQ(tokens[7].text, "1.5e3");
}

TEST(LexerTest, OperatorsUseLongestMatch) {
  const auto tokens = lex_all("a == b => c = d", simple_rules());
  ASSERT_EQ(tokens.size(), 7u);
  EXPECT_EQ(tokens[1].text, "==");
  EXPECT_EQ(tokens[3].text, "=>");
  EXPECT_EQ(tokens[5].text, "=");
}

TEST(LexerTest, StringsKeepEscapesAndReportUnterminated) {
  auto tokens = lex_all(R"("a\"b" 'c')", simple_rules());
  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].kind, token_kind::string);
  EXPECT_EQ(tokens[0].text, R"("a\"b")");

  tokens = lex_all("\"open", simple_rules());
  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].kind, token_kind::error);

  tokens = lex_all("\"line\nnext\"", simple_rules());
  EXPECT_EQ(tokens[0].kind, token_kind::error);
}

TEST(LexerTest, CommentsAreSkippedOrEmitted) {
  auto tokens = lex_all("a // x\n/* y */ b", simple_rules());
  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[1].text, "b");

  lexer_rules keep = simple_rules();
  keep.skip_comments = false;
  tokens = lex_all("a // x\n/* y */ b", keep);
  ASSERT_EQ(tokens.size(), 4u);
  EXPECT_EQ(tokens[1].kind, token_kind::comment);
  EXPECT_EQ(tokens[1].text, "// x");
  EXPECT_EQ(tokens[2].text, "/* y */");

  tokens = lex_all("/* never closed", simple_rules());
  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].kind, token_kind::error);
}

TEST(LexerTest, NestedBlockCommentsNeedMatchingCloses) {
  lexer_rules nested = simple_rules();
  nested.nested_block_comments = true;
  nested.skip_comments = false;

  auto tokens = lex_all("/* a /* b */ c */ d", nested);
  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].text, "/* a /* b */ c */");

  tokens = lex_all("/* a /* b */ c", nested);
  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].kind, token_kind::error);

  // Without nesting the first close ends the comment.
  lexer_rules flat = simple_rules();
  flat.skip_comments = false;
  tokens = lex_all("/* a /* b */ c */", flat);
  EXPECT_EQ(tokens[0].text, "/* a /* b */");
}

TEST(LexerTest, MalformedNumbersAreErrors) {
  {
    const auto tokens = lex_all("12ab", simple_rules());
    ASSERT_FALSE(tokens.empty());
    EXPECT_EQ(tokens[0].kind, token_kind::error);
  }
  {
    const auto tokens = lex_all("0x", simple_rules());
    ASSERT_FALSE(tokens.empty());
    EXPECT_EQ(tokens[0].kind, token_kind::error);
  }
  {
    const auto tokens = lex_all("1_", simple_rules());
    ASSERT_FALSE(tokens.empty());
    EXPECT_EQ(tokens[0].kind, token_kind::error);
  }
}

TEST(LexerTest, CustomIdentifierBytes) {
  lexer_rules r = simple_rules();
  r.identifier_start_extra = "$-";
  r.identifier_continue_extra = "-";
  const auto tokens = lex_all("$home --flag a-b", r);
  ASSERT_EQ(tokens.size(), 3u);
  EXPECT_EQ(tokens[0].text, "$home");
  EXPECT_EQ(tokens[1].text, "--flag");
  EXPECT_EQ(tokens[2].text, "a-b");
}

TEST(LexerTest, TracksLineColumnAndOffset) {
  const auto tokens = lex_all("a\r\n  bb\ncc", simple_rules());
  ASSERT_EQ(tokens.size(), 3u);
  EXPECT_EQ(tokens[1].line, 2u);
  EXPECT_EQ(tokens[1].column, 3u);
  EXPECT_EQ(tokens[1].offset, 5u);
  EXPECT_EQ(tokens[2].line, 3u);
  EXPECT_EQ(tokens[2].column, 1u);
}

TEST(LexerTest, PeekResetAndEnd) {
  lexer lx{"a b", simple_rules()};
  EXPECT_EQ(lx.peek().text, "a");
  EXPECT_EQ(lx.next().text, "a");
  EXPECT_EQ(lx.next().text, "b");
  EXPECT_EQ(lx.next().kind, token_kind::end);
  EXPECT_TRUE(lx.at_end());
  EXPECT_EQ(lx.next().kind, token_kind::end);
  lx.reset();
  EXPECT_FALSE(lx.at_end());
  EXPECT_EQ(lx.next().text, "a");
}

TEST(LexerTest, TokensTileInputWhenNothingIsSkippedAndAlwaysTerminate) {
  lexer_rules r = microfmt::rules::c_like;
  r.skip_whitespace = false;
  r.skip_comments = false;

  const auto check = [&r](string_view input) {
    const auto tokens = lex_all(input, r);
    size_t covered = 0;
    for (const token &t : tokens) {
      EXPECT_EQ(t.offset, covered);
      covered += t.text.size();
      EXPECT_GT(t.text.size(), 0u);
    }
    EXPECT_EQ(covered, input.size());
  };

  // Every byte value, alone and surrounded, plus a nasty mixed input.
  for (string_view input : {string_view{"a /* b */ \"x\\\" 0x 1e+ .5. // z\r\n'q"}, string_view{"\"\\"},
                            string_view{"/*"}, string_view{"0b2"}, string_view{"\r"}}) {
    check(input);
  }
  for (int byte = 0; byte < 256; ++byte) {
    reloco::string alone;
    ASSERT_TRUE(alone.try_push_back(static_cast<char>(byte)));
    check(alone.view());

    reloco::string surrounded;
    ASSERT_TRUE(surrounded.try_push_back('a'));
    ASSERT_TRUE(surrounded.try_push_back(static_cast<char>(byte)));
    ASSERT_TRUE(surrounded.try_push_back('1'));
    check(surrounded.view());
  }
}

TEST(LexerTest, PresetsLexRepresentativeInput) {
  auto tokens = lex_all(R"({"a": [1, 2.5, true, null]})", microfmt::rules::json);
  EXPECT_EQ(tokens[1].kind, token_kind::string);
  EXPECT_EQ(tokens[6].kind, token_kind::floating);
  EXPECT_EQ(tokens[8].kind, token_kind::keyword);

  tokens = lex_all("if x && y; then echo --flag-a # c", microfmt::rules::shell);
  EXPECT_EQ(tokens[0].kind, token_kind::keyword);
  EXPECT_EQ(tokens[3].kind, token_kind::identifier);
  EXPECT_EQ(tokens[tokens.size() - 1].text, "--flag-a");

  tokens = lex_all("for (;;) x->y <<= 1;", microfmt::rules::c_like);
  EXPECT_EQ(tokens[0].kind, token_kind::keyword);
  EXPECT_EQ(tokens[6].text, "->");
  EXPECT_EQ(tokens[8].text, "<<=");
}

TEST(LexerTest, RulesValidation) {
  lexer_rules r;
  EXPECT_TRUE(microfmt::lexer_rules_valid(r));
  r.block_comment_open = "/*";
  EXPECT_FALSE(microfmt::lexer_rules_valid(r));
  r.block_comment_close = "*/";
  EXPECT_TRUE(microfmt::lexer_rules_valid(r));
  r.identifier_start_extra = "1";
  EXPECT_FALSE(microfmt::lexer_rules_valid(r));
}

static_assert(microfmt::lexer_rules_valid(microfmt::rules::c_like));
static_assert(microfmt::lexer{"a b", microfmt::rules::c_like}.peek().kind == token_kind::identifier);
