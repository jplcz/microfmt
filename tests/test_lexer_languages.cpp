// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/parsing/lexer.hpp>
#include <microfmt/parsing/lexer_rules_presets.hpp>
#include <reloco/array.hpp>
#include <reloco/string.hpp>
#include <reloco/vector.hpp>

using microfmt::lexer;
using microfmt::lexer_rules;
using microfmt::string_view;
using microfmt::token;
using microfmt::token_kind;

namespace {

reloco::vector<token> lex_all(string_view text, const lexer_rules &rules) {
  lexer lx{text, rules};
  reloco::vector<token> tokens;
  for (token t = lx.next(); t.kind != token_kind::end; t = lx.next()) {
    if (!tokens.try_push_back(t) || tokens.size() > text.size() + 1) {
      break;
    }
  }
  return tokens;
}

size_t count_kind(const reloco::vector<token> &tokens, token_kind kind) {
  size_t count = 0;
  for (const token &t : tokens) {
    count += t.kind == kind ? 1U : 0U;
  }
  return count;
}

// Valid source must never yield an error token: the upper layer relies on that.
void expect_no_errors(string_view text, const lexer_rules &rules) {
  const auto tokens = lex_all(text, rules);
  EXPECT_FALSE(tokens.empty());
  EXPECT_EQ(count_kind(tokens, token_kind::error), 0u);
}

} // namespace

TEST(LexerLanguagesTest, CDirectivesCharsPrefixesAndSuffixes) {
  const auto tokens = lex_all("#include <x.h>\n  #define A \\\n  1\nint x = 10UL + 0x1.8p3 + 1.5f; char c = L'\\'';",
                              microfmt::rules::c);
  EXPECT_EQ(tokens[0].kind, token_kind::directive);
  EXPECT_EQ(tokens[0].text, "#include <x.h>");
  EXPECT_EQ(tokens[1].kind, token_kind::directive);
  EXPECT_EQ(tokens[1].text, "#define A \\\n  1");
  EXPECT_EQ(count_kind(tokens, token_kind::error), 0u);
  EXPECT_EQ(count_kind(tokens, token_kind::character), 1u);
  EXPECT_EQ(count_kind(tokens, token_kind::floating), 2u);
  EXPECT_EQ(count_kind(tokens, token_kind::integer), 1u);
}

TEST(LexerLanguagesTest, CHashMidLineIsNotADirective) {
  const auto tokens = lex_all("a # b", microfmt::rules::c);
  EXPECT_EQ(count_kind(tokens, token_kind::directive), 0u);
  EXPECT_EQ(tokens[1].kind, token_kind::error);
}

TEST(LexerLanguagesTest, CppRawStringsDigitSeparatorsAndUserLiterals) {
  const auto tokens = lex_all(R"src(auto s = R"x(a)" )y" "q)x"; int n = 1'000'000; auto d = 10_km + "s"_str + u8"t"; a::b <=> c;)src",
                              microfmt::rules::cpp);
  EXPECT_EQ(count_kind(tokens, token_kind::error), 0u);
  EXPECT_EQ(tokens[3].kind, token_kind::string);
  EXPECT_EQ(tokens[3].text, R"src(R"x(a)" )y" "q)x")src");
  EXPECT_EQ(count_kind(tokens, token_kind::integer), 2u); // 1'000'000 and 10_km
  EXPECT_EQ(count_kind(tokens, token_kind::string), 3u);
  bool found_user_literal = false;
  bool found_scope_op = false;
  for (const token &t : tokens) {
    found_user_literal = found_user_literal || t.text == "10_km";
    found_scope_op = found_scope_op || (t.text == "::" && t.kind == token_kind::op);
  }
  EXPECT_TRUE(found_user_literal);
  EXPECT_TRUE(found_scope_op);
}

TEST(LexerLanguagesTest, CppUnterminatedRawStringIsError) {
  const auto tokens = lex_all("R\"d(never closed", microfmt::rules::cpp);
  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].kind, token_kind::error);
}

TEST(LexerLanguagesTest, CLikeAliasMatchesCpp) {
  const auto tokens = lex_all("'a'", microfmt::rules::c_like);
  EXPECT_EQ(tokens[0].kind, token_kind::character);
}

TEST(LexerLanguagesTest, LuaLongBracketsCommentsAndNumbers) {
  const auto tokens = lex_all("--[==[ a ]] b ]==] x = [[l1\nl2]] .. a[b[1]] --[ not long\ny = 0x1p4 // 2", microfmt::rules::lua);
  EXPECT_EQ(count_kind(tokens, token_kind::error), 0u);
  EXPECT_EQ(count_kind(tokens, token_kind::string), 1u);
  EXPECT_EQ(tokens[2].text, "[[l1\nl2]]");
  EXPECT_EQ(count_kind(tokens, token_kind::floating), 1u);

  const auto trailing = lex_all("4. 2.E-1 1..2", microfmt::rules::lua);
  ASSERT_EQ(trailing.size(), 5u); // `4.`, `2.E-1`, `1`, `..`, `2`
  EXPECT_EQ(trailing[0].text, "4.");
  EXPECT_EQ(trailing[0].kind, token_kind::floating);
  EXPECT_EQ(trailing[1].text, "2.E-1");
  EXPECT_EQ(trailing[2].kind, token_kind::integer);

  const auto unterminated = lex_all("--[=[ open", microfmt::rules::lua);
  ASSERT_EQ(unterminated.size(), 1u);
  EXPECT_EQ(unterminated[0].kind, token_kind::error);

  lexer_rules keep = microfmt::rules::lua;
  keep.skip_comments = false;
  const auto kept = lex_all("--[[x]] y", keep);
  ASSERT_FALSE(kept.empty());
  EXPECT_EQ(kept[0].kind, token_kind::comment);
}

TEST(LexerLanguagesTest, SqlKeywordsAreCaseInsensitiveAndQuotesDouble) {
  const auto tokens = lex_all("SeLeCt `a b`, \"c\"\"d\", 'it''s\nok' FROM t -- c\nWHERE x <> 1", microfmt::rules::sql);
  EXPECT_EQ(count_kind(tokens, token_kind::error), 0u);
  EXPECT_EQ(count_kind(tokens, token_kind::keyword), 3u);
  EXPECT_EQ(count_kind(tokens, token_kind::string), 3u);
  EXPECT_EQ(tokens[0].text, "SeLeCt");
}

TEST(LexerLanguagesTest, IniAndConfigValuesAreBareWordsWithoutErrors) {
  expect_no_errors("[core]\n; c1\n# c2\npath = /usr/local/bin\nip=192.168.0.1\nurl: http://x.y:80/a?b=1\n", microfmt::rules::ini);
  expect_no_errors("server {\n  listen 80; # c\n  location ~ ^/(a|b)$ { proxy_pass http://127.0.0.1:8080; }\n}\n",
                   microfmt::rules::config);
  expect_no_errors("net.ipv4.ip_forward = 1\nPATH=/usr/bin:$PATH\n; c\nname = \"x y\"\n", microfmt::rules::config_kv);

  const auto tokens = lex_all("a = 192.168.0.1 ; c", microfmt::rules::ini);
  EXPECT_EQ(tokens[2].kind, token_kind::identifier);
  EXPECT_EQ(tokens[2].text, "192.168.0.1");
  EXPECT_EQ(tokens.size(), 3u);
}

TEST(LexerLanguagesTest, TomlStringsNumbersAndDates) {
  const auto tokens = lex_all(
      "a-b = 'C:\\x'\ns = \"\"\"l1\nl2\"\"\"\nr = '''\nraw\\'''\nd = 1979-05-27T07:32:00Z\nt = 07:32:00.5\nd2 = 1979-05-27 07:32:00+01:00\n"
      "o = 0o17\nk = 2024-key\ni = 1_000\nf = +inf # c\n[[tbl]]",
      microfmt::rules::toml);
  EXPECT_EQ(count_kind(tokens, token_kind::error), 0u);
  EXPECT_EQ(count_kind(tokens, token_kind::datetime), 3u);
  EXPECT_EQ(tokens[2].kind, token_kind::string);
  EXPECT_EQ(tokens[2].text, "'C:\\x'");
  EXPECT_EQ(tokens[0].text, "a-b");
}

TEST(LexerLanguagesTest, ExistingPresetsStillAccept) {
  expect_no_errors("{\"a\": [1, 2.5e3, true, null]}", microfmt::rules::json);
  expect_no_errors("if [ -f x ]; then echo $a --b >> f; fi # c", microfmt::rules::shell);
}

TEST(LexerLanguagesTest, EveryPresetTilesInputAndTerminatesForAllBytes) {
  const reloco::array<lexer_rules, 10> presets{microfmt::rules::c,   microfmt::rules::cpp,  microfmt::rules::lua,
                                               microfmt::rules::sql, microfmt::rules::toml, microfmt::rules::ini,
                                               microfmt::rules::config, microfmt::rules::config_kv,
                                               microfmt::rules::json, microfmt::rules::shell};
  for (const lexer_rules &preset : presets) {
    lexer_rules r = preset;
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
    for (string_view input : {string_view{"R\"(x"}, string_view{"--[==[ x"}, string_view{"'''x"}, string_view{"[=[x"},
                              string_view{"#a\\\n\\"}, string_view{"0x.p"}, string_view{"1979-05-27T"}, string_view{"'a''"}}) {
      check(input);
    }
    for (int byte = 0; byte < 256; ++byte) {
      reloco::string text;
      for (const char lead : {'a', '1', '['}) {
        ASSERT_TRUE(text.try_push_back(lead));
        ASSERT_TRUE(text.try_push_back(static_cast<char>(byte)));
        ASSERT_TRUE(text.try_push_back('1'));
      }
      check(text.view());
    }
  }
}

TEST(LexerLanguagesTest, RuleValidationRejectsInconsistentExtensions) {
  lexer_rules r;
  r.long_bracket_comments = true;
  EXPECT_FALSE(microfmt::lexer_rules_valid(r));

  r = {};
  r.cpp_raw_strings = true;
  EXPECT_FALSE(microfmt::lexer_rules_valid(r));

  r = {};
  r.quotes = "'";
  r.char_quotes = "'";
  EXPECT_FALSE(microfmt::lexer_rules_valid(r));

  r = microfmt::rules::c;
  r.line_comment = "#";
  EXPECT_FALSE(microfmt::lexer_rules_valid(r));

  r = {};
  r.keywords_ignore_case = true;
  constexpr microfmt::keyword_table upper{"SELECT"};
  r.keywords = upper;
  EXPECT_FALSE(microfmt::lexer_rules_valid(r));
}
