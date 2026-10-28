// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/parsing/lexer_rules_presets.hpp>
#include <microfmt/parsing/token_stream.hpp>

using microfmt::depth_guard;
using microfmt::lexer_rules;
using microfmt::parse_error;
using microfmt::string_view;
using microfmt::token_kind;
using microfmt::token_stream;

namespace {

// JSON numbers may start with '-', which the json preset does not lex as part of a number.
constexpr string_view kMinus[] = {"-"};

lexer_rules json_rules() {
  lexer_rules r = microfmt::rules::json;
  r.operators = kMinus;
  return r;
}

constexpr size_t kMaxDepth = 8;

reloco::expected<void, parse_error> parse_value(token_stream &ts, size_t &depth);

reloco::expected<void, parse_error> parse_sequence(token_stream &ts, size_t &depth, bool object) {
  const string_view close = object ? "}" : "]";
  ts.next(); // opening bracket
  if (ts.skip(token_kind::punctuation, close)) {
    return {};
  }
  for (;;) {
    if (object) {
      if (auto key = ts.expect(token_kind::string, "expected object key"); !key) {
        return reloco::unexpected<parse_error>(key.error());
      }
      if (auto colon = ts.expect(token_kind::punctuation, ":", "expected ':'"); !colon) {
        return reloco::unexpected<parse_error>(colon.error());
      }
    }
    if (auto v = parse_value(ts, depth); !v) {
      return v;
    }
    if (ts.skip(token_kind::punctuation, ",")) {
      continue;
    }
    if (auto end = ts.expect(token_kind::punctuation, close, "expected ',' or closing bracket"); !end) {
      return reloco::unexpected<parse_error>(end.error());
    }
    return {};
  }
}

reloco::expected<void, parse_error> parse_value(token_stream &ts, size_t &depth) {
  depth_guard guard{depth, kMaxDepth};
  if (!guard) {
    return reloco::unexpected<parse_error>(ts.error_here("nesting too deep"));
  }
  if (ts.check(token_kind::punctuation, "{")) {
    return parse_sequence(ts, depth, true);
  }
  if (ts.check(token_kind::punctuation, "[")) {
    return parse_sequence(ts, depth, false);
  }
  ts.skip(token_kind::op, "-");
  if (ts.skip(token_kind::string) || ts.skip(token_kind::integer) || ts.skip(token_kind::floating) ||
      ts.skip(token_kind::keyword)) {
    return {};
  }
  return reloco::unexpected<parse_error>(ts.error_here("expected a value"));
}

reloco::expected<void, parse_error> validate(string_view text) {
  token_stream ts{text, json_rules()};
  size_t depth = 0;
  if (auto v = parse_value(ts, depth); !v) {
    return v;
  }
  if (!ts.at_end()) {
    return reloco::unexpected<parse_error>(ts.error_here("trailing input"));
  }
  return {};
}

} // namespace

TEST(TokenStream, LookaheadDoesNotConsume) {
  token_stream ts{"a b c", microfmt::rules::json};
  EXPECT_EQ(ts.peek().text, "a");
  EXPECT_EQ(ts.peek2().text, "b");
  EXPECT_EQ(ts.next().text, "a");
  EXPECT_EQ(ts.peek().text, "b");
  EXPECT_EQ(ts.peek2().text, "c");
  EXPECT_EQ(ts.next().text, "b");
  EXPECT_EQ(ts.next().text, "c");
  EXPECT_TRUE(ts.at_end());
  EXPECT_EQ(ts.peek2().kind, token_kind::end);
  EXPECT_EQ(ts.next().kind, token_kind::end);
}

TEST(TokenStream, AcceptConsumesOnlyOnMatch) {
  token_stream ts{"x : 1", microfmt::rules::json};
  EXPECT_FALSE(ts.skip(token_kind::integer));
  EXPECT_FALSE(ts.skip(token_kind::punctuation, ":"));
  auto id = ts.accept(token_kind::identifier);
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(id->text, "x");
  EXPECT_TRUE(ts.skip(token_kind::punctuation, ":"));
  EXPECT_TRUE(ts.skip(token_kind::integer));
  EXPECT_TRUE(ts.at_end());
}

TEST(TokenStream, ExpectReportsPositionOfOffendingToken) {
  token_stream ts{"[1,\n  @]", microfmt::rules::json};
  ASSERT_TRUE(ts.expect(token_kind::punctuation, "[", "need [").has_value());
  ASSERT_TRUE(ts.expect(token_kind::integer, "need int").has_value());
  ASSERT_TRUE(ts.expect(token_kind::punctuation, ",", "need ,").has_value());
  auto bad = ts.expect(token_kind::integer, "need int");
  ASSERT_FALSE(bad.has_value());
  EXPECT_EQ(bad.error().message, "need int");
  EXPECT_EQ(bad.error().at.kind, token_kind::error); // '@' is unknown to the lexer
  EXPECT_EQ(bad.error().at.line, 2u);
  EXPECT_EQ(bad.error().at.column, 3u);
  EXPECT_EQ(ts.peek().text, "@"); // a failed expect does not consume
}

TEST(TokenStream, CopyIsBacktracking) {
  token_stream ts{"a b", microfmt::rules::json};
  const token_stream saved = ts;
  ts.next();
  ts.next();
  EXPECT_TRUE(ts.at_end());
  ts = saved;
  EXPECT_EQ(ts.next().text, "a");
}

TEST(TokenStream, DepthGuardCountsAndReleases) {
  size_t depth = 0;
  {
    depth_guard a{depth, 2};
    depth_guard b{depth, 2};
    EXPECT_TRUE(a);
    EXPECT_TRUE(b);
    depth_guard c{depth, 2};
    EXPECT_FALSE(c.ok());
    EXPECT_EQ(depth, 3u);
  }
  EXPECT_EQ(depth, 0u);
}

TEST(TokenStream, JsonValidatorAcceptsValidDocuments) {
  EXPECT_TRUE(validate("{\"a\": [1, -2.5, true, null, \"s\"], \"b\": {}}").has_value());
  EXPECT_TRUE(validate("[]").has_value());
}

TEST(TokenStream, JsonValidatorRejectsInvalidDocuments) {
  auto missing_colon = validate("{\"a\" 1}");
  ASSERT_FALSE(missing_colon.has_value());
  EXPECT_EQ(missing_colon.error().message, "expected ':'");
  EXPECT_EQ(missing_colon.error().at.column, 6u);

  EXPECT_FALSE(validate("[1, 2").has_value());
  EXPECT_FALSE(validate("[1] 2").has_value());
  EXPECT_FALSE(validate("").has_value());
  EXPECT_FALSE(validate("\"unterminated").has_value());
}

TEST(TokenStream, DepthLimitStopsHostileNesting) {
  auto r = validate("[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]");
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().message, "nesting too deep");
}
