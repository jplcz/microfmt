// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include <microfmt/parsing/ast_pool.hpp>
#include <microfmt/parsing/lexer_rules_presets.hpp>
#include <microfmt/parsing/number_parse.hpp>
#include <microfmt/parsing/string_pool.hpp>
#include <microfmt/parsing/token_stream.hpp>

using microfmt::assoc;
using microfmt::binary_op;
using microfmt::depth_guard;
using microfmt::lexer_rules;
using microfmt::parse_error;
using microfmt::prefix_op;
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

namespace {

constexpr string_view kCalcOps[] = {"+", "-", "*", "/", "^"};
constexpr binary_op kBinary[] = {{"+", 10, assoc::left},
                                 {"-", 10, assoc::left},
                                 {"*", 20, assoc::left},
                                 {"/", 20, assoc::left},
                                 {"^", 40, assoc::right}};
constexpr prefix_op kPrefix[] = {{"-", 30}};

using calc_result = reloco::expected<int64_t, parse_error>;

calc_result calc(string_view text, size_t max_depth = 64) {
  lexer_rules rules;
  rules.operators = kCalcOps;
  token_stream ts{text, rules};
  auto primary = [](token_stream &s) -> calc_result {
    if (!s.check(token_kind::integer)) {
      return reloco::unexpected<parse_error>(s.error_here("expected a number"));
    }
    return microfmt::parse<int64_t>(s.next().text).value();
  };
  auto binary = [&ts](const binary_op &op, int64_t l, int64_t r) -> calc_result {
    switch (op.text[0]) {
    case '+':
      return l + r;
    case '-':
      return l - r;
    case '*':
      return l * r;
    case '/':
      if (r == 0) {
        return reloco::unexpected<parse_error>(ts.error_here("division by zero"));
      }
      return l / r;
    default: {
      int64_t result = 1;
      for (int64_t i = 0; i < r; ++i) {
        result *= l;
      }
      return result;
    }
    }
  };
  auto prefix = [](const prefix_op &, int64_t v) -> calc_result { return -v; };
  auto result = microfmt::parse_expression<int64_t>(ts, kBinary, kPrefix, primary, binary, prefix, max_depth);
  if (result && !ts.at_end()) {
    return reloco::unexpected<parse_error>(ts.error_here("trailing input"));
  }
  return result;
}

} // namespace

TEST(Expression, PrecedenceAndAssociativity) {
  EXPECT_EQ(calc("1 + 2 * 3").value(), 7);
  EXPECT_EQ(calc("(1 + 2) * 3").value(), 9);
  EXPECT_EQ(calc("10 - 4 - 3").value(), 3);  // left associative
  EXPECT_EQ(calc("2 ^ 3 ^ 2").value(), 512); // right associative
  EXPECT_EQ(calc("8 / 2 / 2").value(), 2);
}

TEST(Expression, PrefixBindsTighterThanMultiplyButLooserThanPower) {
  EXPECT_EQ(calc("-2 * 3").value(), -6);
  EXPECT_EQ(calc("-2 ^ 2").value(), -4);
  EXPECT_EQ(calc("1 - -4").value(), 5);
  EXPECT_EQ(calc("--3").value(), 3);
}

TEST(Expression, ReportsErrorsAtTheOffendingToken) {
  auto missing_operand = calc("1 + ");
  ASSERT_FALSE(missing_operand.has_value());
  EXPECT_EQ(missing_operand.error().message, "expected a number");
  EXPECT_EQ(missing_operand.error().at.kind, token_kind::end);

  auto unclosed = calc("(1 + 2");
  ASSERT_FALSE(unclosed.has_value());
  EXPECT_EQ(unclosed.error().message, "expected ')'");

  EXPECT_EQ(calc("1 2").error().message, "trailing input");
  EXPECT_EQ(calc("1 / 0").error().message, "division by zero");
}

TEST(Expression, DepthLimitStopsHostileNesting) {
  EXPECT_TRUE(calc("((((1))))", 16).has_value());
  auto deep = calc("((((((((((((((((((((1))))))))))))))))))))", 8);
  ASSERT_FALSE(deep.has_value());
  EXPECT_EQ(deep.error().message, "nesting too deep");
}

TEST(ParseList, AcceptsEmptySingleAndMultipleItems) {
  auto count_items = [](string_view text, microfmt::list_options opt) {
    token_stream ts{text, microfmt::rules::json};
    size_t items = 0;
    auto r = microfmt::parse_list(ts, opt, [&items](token_stream &s) -> reloco::expected<void, parse_error> {
      if (auto n = s.expect(token_kind::integer, "expected a number"); !n) {
        return reloco::unexpected<parse_error>(n.error());
      }
      ++items;
      return {};
    });
    return r.has_value() ? static_cast<int>(items) : -1;
  };
  EXPECT_EQ(count_items("[]", {}), 0);
  EXPECT_EQ(count_items("[1]", {}), 1);
  EXPECT_EQ(count_items("[1, 2, 3]", {}), 3);
  EXPECT_EQ(count_items("[1, 2,]", {}), -1);
  microfmt::list_options trailing;
  trailing.allow_trailing = true;
  EXPECT_EQ(count_items("[1, 2,]", trailing), 2);
  EXPECT_EQ(count_items("[1 2]", {}), -1);
  EXPECT_EQ(count_items("[1, 2", {}), -1);
  EXPECT_EQ(count_items("1, 2", {.open = "", .close = ""}), 2); // bare list
}

TEST(ParseList, ReportsMissingSeparatorAtTheOffendingToken) {
  token_stream ts{"[1 2]", microfmt::rules::json};
  auto r = microfmt::parse_list(ts, {}, [](token_stream &s) -> reloco::expected<void, parse_error> {
    if (auto n = s.expect(token_kind::integer, "expected a number"); !n) {
      return reloco::unexpected<parse_error>(n.error());
    }
    return {};
  });
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().message, "expected separator or closing bracket");
  EXPECT_EQ(r.error().at.column, 4u);
}

namespace {

constexpr string_view kPostfixOps[] = {"+", "-", "#", "["};

// Evaluates with postfix `#` (adds 100) and `[` index `]` (operand * 10 + index).
calc_result calc_post(string_view text, uint8_t hash_precedence) {
  lexer_rules rules;
  rules.operators = kPostfixOps;
  token_stream ts{text, rules};
  const microfmt::postfix_op postfix[] = {{"#", hash_precedence}, {"[", 60}};
  auto primary = [](token_stream &s) -> calc_result {
    if (!s.check(token_kind::integer)) {
      return reloco::unexpected<parse_error>(s.error_here("expected a number"));
    }
    return microfmt::parse<int64_t>(s.next().text).value();
  };
  auto binary = [](const binary_op &, int64_t l, int64_t r) -> calc_result { return l + r; };
  auto prefix = [](const prefix_op &, int64_t v) -> calc_result { return -v; };
  auto make_postfix = [](const microfmt::postfix_op &op, int64_t v, token_stream &s) -> calc_result {
    if (op.text == "#") {
      return v + 100;
    }
    auto index = s.expect(token_kind::integer, "expected an index");
    if (!index) {
      return reloco::unexpected<parse_error>(index.error());
    }
    if (auto close = s.expect(token_kind::punctuation, "]", "expected ']'"); !close) {
      return reloco::unexpected<parse_error>(close.error());
    }
    return v * 10 + microfmt::parse<int64_t>(index.value().text).value();
  };
  constexpr binary_op plus[] = {{"+", 10}};
  constexpr prefix_op minus[] = {{"-", 30}};
  return microfmt::parse_expression<int64_t>(ts, plus, minus, postfix, primary, binary, prefix, make_postfix);
}

} // namespace

TEST(Expression, PostfixOperatorsRespectPrecedence) {
  EXPECT_EQ(calc_post("3#", 50).value(), 103);
  EXPECT_EQ(calc_post("-3#", 50).value(), -103); // # binds tighter than prefix -: -(3#)
  EXPECT_EQ(calc_post("-3#", 10).value(), 97);   // # looser than prefix -: (-3)#
  EXPECT_EQ(calc_post("1 + 2#", 50).value(), 103);
  EXPECT_EQ(calc_post("5[2]", 50).value(), 52); // callback consumes the index and ']'
  EXPECT_EQ(calc_post("5[2][3]", 50).value(), 523);
}

TEST(Expression, PostfixCallbackErrorsPropagate) {
  auto r = calc_post("5[x]", 50);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().message, "expected an index");
  auto unclosed = calc_post("5[2", 50);
  ASSERT_FALSE(unclosed.has_value());
  EXPECT_EQ(unclosed.error().message, "expected ']'");
}

namespace {

constexpr microfmt::mixfix_op kMixfixForms[] = {{"if", 5}};
constexpr binary_op kMixfixBinary[] = {{"+", 10, assoc::left}, {"*", 20, assoc::left}};
constexpr prefix_op kMixfixPrefix[] = {{"-", 30}};
constexpr microfmt::postfix_op kMixfixPostfix[] = {{"#", 50}};

// `if c then a else b` evaluates to a or b; `#` adds 100; `?:` is available too.
struct mixfix_actions {
  unsigned else_precedence_seen{0};

  calc_result primary(token_stream &s) {
    if (!s.check(token_kind::integer)) {
      return reloco::unexpected<parse_error>(s.error_here("expected a number"));
    }
    return microfmt::parse<int64_t>(s.next().text).value();
  }
  calc_result make_binary(const binary_op &op, int64_t l, int64_t r) { return op.text == "+" ? l + r : l * r; }
  calc_result make_prefix(const prefix_op &, int64_t v) { return -v; }
  calc_result make_postfix(const microfmt::postfix_op &, int64_t v, token_stream &) { return v + 100; }
  calc_result make_ternary(const microfmt::ternary_op &, int64_t c, int64_t t, int64_t e) { return c != 0 ? t : e; }
  template <typename Expr> calc_result make_mixfix(const microfmt::mixfix_op &op, token_stream &s, Expr &expr) {
    auto cond = expr.parse(0);
    if (!cond) {
      return cond;
    }
    if (auto kw = s.expect(token_kind::identifier, "then", "expected 'then'"); !kw) {
      return reloco::unexpected<parse_error>(kw.error());
    }
    auto a = expr.parse(0);
    if (!a) {
      return a;
    }
    if (auto kw = s.expect(token_kind::identifier, "else", "expected 'else'"); !kw) {
      return reloco::unexpected<parse_error>(kw.error());
    }
    auto b = expr.parse(trailing_precedence.value_or(op.precedence));
    if (!b) {
      return b;
    }
    return cond.value() != 0 ? a.value() : b.value();
  }
  reloco::optional<unsigned> trailing_precedence{};
};

calc_result mixfix_calc(string_view text, size_t max_depth = 64, reloco::optional<unsigned> trailing = {}) {
  lexer_rules rules;
  rules.operators = kCalcOps;
  token_stream ts{text, rules};
  mixfix_actions actions;
  actions.trailing_precedence = trailing;
  const microfmt::expression_grammar grammar{kMixfixBinary, kMixfixPrefix, {}, kMixfixForms, {}};
  auto result = microfmt::parse_expression<int64_t>(ts, grammar, actions, max_depth);
  if (result && !ts.at_end()) {
    return reloco::unexpected<parse_error>(ts.error_here("trailing input"));
  }
  return result;
}

} // namespace

TEST(Mixfix, IfThenElseEvaluatesAndNests) {
  EXPECT_EQ(mixfix_calc("if 1 then 10 else 20").value(), 10);
  EXPECT_EQ(mixfix_calc("if 0 then 10 else 20").value(), 20);
  EXPECT_EQ(mixfix_calc("if 1 then if 0 then 1 else 2 else 3").value(), 2);
  EXPECT_EQ(mixfix_calc("if 0 then 1 else if 0 then 2 else 3").value(), 3);
}

TEST(Mixfix, WorksAsOperandInsideBinaryPrefixAndParentheses) {
  EXPECT_EQ(mixfix_calc("1 + if 1 then 2 else 3 * 4").value(), 3);
  EXPECT_EQ(mixfix_calc("2 * (if 0 then 1 else 5) + 1").value(), 11);
  EXPECT_EQ(mixfix_calc("-if 1 then 7 else 0").value(), -7);
}

TEST(Mixfix, TrailingPrecedenceControlsHowFarElseExtends) {
  // Low hint: the else branch swallows `+ 1`. High hint: it stops, so `+ 1` applies to the whole form.
  EXPECT_EQ(mixfix_calc("if 0 then 5 else 2 + 1").value(), 3);
  EXPECT_EQ(mixfix_calc("if 0 then 5 else 2 + 1", 64, 30u).value(), 3);
  EXPECT_EQ(mixfix_calc("1 + if 1 then 5 else 2 + 1", 64, 5u).value(), 6);  // (2 + 1) belongs to the else branch
  EXPECT_EQ(mixfix_calc("1 + if 1 then 5 else 2 + 1", 64, 30u).value(), 7); // (1 + 5) + 1
}

TEST(Mixfix, MissingKeywordsAreReportedAtTheRightToken) {
  auto no_then = mixfix_calc("if 1 5 else 2");
  ASSERT_FALSE(no_then.has_value());
  EXPECT_EQ(no_then.error().message, "expected 'then'");
  EXPECT_EQ(no_then.error().at.text, "5");

  auto no_else = mixfix_calc("if 1 then 2");
  ASSERT_FALSE(no_else.has_value());
  EXPECT_EQ(no_else.error().message, "expected 'else'");
  EXPECT_EQ(no_else.error().at.kind, token_kind::end);
}

TEST(Mixfix, OtherIdentifiersStillReachPrimaryAndDepthIsBounded) {
  auto unknown = mixfix_calc("iffy");
  ASSERT_FALSE(unknown.has_value());
  EXPECT_EQ(unknown.error().message, "expected a number");

  auto deep = mixfix_calc("if 1 then if 1 then if 1 then if 1 then if 1 then 1 else 1 else 1 else 1 else 1 else 1", 4);
  ASSERT_FALSE(deep.has_value());
  EXPECT_EQ(deep.error().message, "nesting too deep");
}

TEST(Mixfix, CombinesWithEveryOtherGrammarPart) {
  lexer_rules rules;
  constexpr string_view ops[] = {"+", "*", "-", "?", ":", "#"};
  rules.operators = ops;
  token_stream ts{"-1 # + if 0 then 2 else 3 ? 4 : 5", rules};
  mixfix_actions actions;
  const microfmt::expression_grammar grammar{kMixfixBinary, kMixfixPrefix, kMixfixPostfix, kMixfixForms,
                                             microfmt::ternary_op{}};
  auto result = microfmt::parse_expression<int64_t>(ts, grammar, actions);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), -97); // -(1 #) + (else branch: `3 ? 4 : 5` is 4)
  EXPECT_TRUE(ts.at_end());
}

TEST(Mixfix, MissingOptionalCallbackIsAnErrorOnlyWhenUsed) {
  struct minimal {
    calc_result primary(token_stream &s) { return microfmt::parse<int64_t>(s.next().text).value(); }
    calc_result make_binary(const binary_op &, int64_t l, int64_t r) { return l + r; }
  };
  lexer_rules rules;
  rules.operators = kCalcOps;
  token_stream plain{"1 + 2", rules};
  minimal actions;
  const microfmt::expression_grammar only_binary{kMixfixBinary, {}, {}, {}, {}};
  EXPECT_EQ(microfmt::parse_expression<int64_t>(plain, only_binary, actions).value(), 3);

  token_stream used{"if 1 then 2 else 3", rules};
  const microfmt::expression_grammar with_mixfix{kMixfixBinary, {}, {}, kMixfixForms, {}};
  auto r = microfmt::parse_expression<int64_t>(used, with_mixfix, actions);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().message, "no make_mixfix callback");
}

TEST(Spans, PreviousTracksLastConsumedTokenAndSurvivesBacktracking) {
  token_stream ts{"a b c", lexer_rules{}};
  EXPECT_EQ(ts.previous().kind, token_kind::end); // nothing consumed yet
  ts.next();
  EXPECT_EQ(ts.previous().text, "a");
  auto failed = microfmt::attempt(ts, [](token_stream &s) -> reloco::expected<void, parse_error> {
    s.next();
    return reloco::unexpected<parse_error>(s.error_here("nope"));
  });
  EXPECT_FALSE(failed.has_value());
  EXPECT_EQ(ts.previous().text, "a"); // restored with the stream
}

TEST(Spans, SpanCoversTextWithoutSurroundingWhitespaceAndComments) {
  lexer_rules rules;
  rules.line_comment = "//";
  token_stream ts{"  first // note\n   second  \"str\" tail", rules};
  const microfmt::token start = ts.peek();
  ts.next();
  ts.next();
  ts.next(); // the string token
  const microfmt::source_span span = microfmt::span_from(ts, start);
  EXPECT_EQ(span.text, "first // note\n   second  \"str\"");
  EXPECT_EQ(span.offset, 2u);
  EXPECT_EQ(span.line, 1u);
  EXPECT_EQ(span.column, 3u);

  const microfmt::token tail = ts.peek();
  const microfmt::source_span empty = microfmt::span_from(ts, tail); // nothing consumed after `tail`
  EXPECT_TRUE(empty.text.empty());
  EXPECT_EQ(empty.offset, tail.offset);
}

TEST(Spans, ParseSpannedPairsValueWithSpanAndPassesErrorsThrough) {
  token_stream ts{"x\n  42 y", lexer_rules{}};
  auto number = [](token_stream &s) -> reloco::expected<int64_t, parse_error> {
    (void)s.next(); // x
    auto n = s.expect(token_kind::integer, "expected a number");
    if (!n) {
      return reloco::unexpected<parse_error>(n.error());
    }
    return microfmt::parse<int64_t>(n.value().text).value();
  };
  auto ok = microfmt::parse_spanned(ts, number);
  ASSERT_TRUE(ok.has_value());
  EXPECT_EQ(ok.value().value, 42);
  EXPECT_EQ(ok.value().span.text, "x\n  42");
  EXPECT_EQ(ok.value().span.line, 1u);

  auto bad = microfmt::parse_spanned(ts, number); // "y" then end: expected a number
  ASSERT_FALSE(bad.has_value());
  EXPECT_EQ(bad.error().message, "expected a number");
}

namespace {

using text_result = reloco::expected<int64_t, parse_error>;

text_result chain(string_view text, size_t max_items = static_cast<size_t>(-1)) {
  lexer_rules rules;
  rules.operators = kCalcOps;
  token_stream ts{text, rules};
  auto item = [](token_stream &s) -> text_result {
    auto n = s.expect(token_kind::integer, "expected a number");
    if (!n) {
      return reloco::unexpected<parse_error>(n.error());
    }
    return microfmt::parse<int64_t>(n.value().text).value();
  };
  auto fold = [](const microfmt::token &, int64_t l, int64_t r) -> text_result { return l - r; };
  return microfmt::parse_chain<int64_t>(ts, token_kind::op, "-", item, fold, max_items);
}

} // namespace

TEST(Chain, FoldsLeftToRight) {
  EXPECT_EQ(chain("7").value(), 7);
  EXPECT_EQ(chain("10 - 3 - 2").value(), 5); // (10 - 3) - 2, not 10 - (3 - 2)
}

TEST(Chain, ReportsMissingItemsAndEnforcesTheLimit) {
  auto dangling = chain("1 - ");
  ASSERT_FALSE(dangling.has_value());
  EXPECT_EQ(dangling.error().message, "expected a number");
  EXPECT_EQ(dangling.error().at.kind, token_kind::end);

  EXPECT_FALSE(chain("x - 1").has_value()); // first item fails

  EXPECT_EQ(chain("1 - 1 - 1", 3).value(), -1);
  auto too_many = chain("1 - 1 - 1 - 1", 3);
  ASSERT_FALSE(too_many.has_value());
  EXPECT_EQ(too_many.error().message, "too many items in chain");
}

namespace {

constexpr microfmt::bracket_pair kListBrackets[] = {
    {token_kind::punctuation, "(", token_kind::punctuation, ")"},
    {token_kind::punctuation, "[", token_kind::punctuation, "]"}};

struct recovering_outcome {
  bool ok{false};
  size_t failed{0};
  size_t good{0};
  microfmt::error_list<4> errors{};
  string_view message{};
};

// List of integers; anything else is an item error. Opener/closer are "(" and ")".
recovering_outcome recover_list(string_view text, bool allow_trailing = false) {
  token_stream ts{text, lexer_rules{}};
  recovering_outcome out;
  const microfmt::list_options options{"(", ")", ",", allow_trailing};
  auto item = [&out](token_stream &s) -> reloco::expected<void, parse_error> {
    if (auto n = s.expect(token_kind::integer, "expected a number"); !n) {
      return reloco::unexpected<parse_error>(n.error());
    }
    ++out.good;
    return {};
  };
  auto r = microfmt::parse_list_recovering(ts, options, out.errors, kListBrackets, item);
  out.ok = r.has_value();
  if (r) {
    out.failed = r.value();
  } else {
    out.message = r.error().message;
  }
  return out;
}

} // namespace

TEST(ListRecovery, ReportsEveryBadItemAndStillChecksTheRest) {
  const auto out = recover_list("(1, x, 3, y, 5)");
  ASSERT_TRUE(out.ok);
  EXPECT_EQ(out.failed, 2u);
  EXPECT_EQ(out.good, 3u);
  const auto errors = out.errors.errors();
  ASSERT_EQ(errors.size(), 2u);
  EXPECT_EQ(errors[0].at.offset, 4u); // "x": the text is not kept, only the position
  EXPECT_EQ(errors[0].at.length, 1u);
  EXPECT_EQ(errors[1].at.offset, 10u);

  const auto clean = recover_list("(1, 2)");
  EXPECT_TRUE(clean.ok);
  EXPECT_EQ(clean.failed, 0u);
  EXPECT_TRUE(clean.errors.empty());
}

TEST(ListRecovery, SkipsNestedBracketsAsOneUnit) {
  // The commas inside (..) and [..] must not be taken for separators of the outer list.
  const auto out = recover_list("(1, x (a, b) [c, d], 3)");
  ASSERT_TRUE(out.ok);
  EXPECT_EQ(out.failed, 1u);
  EXPECT_EQ(out.good, 2u);
}

TEST(ListRecovery, HandlesEmptyElementsTrailingSeparatorsAndTheLastItem) {
  EXPECT_EQ(recover_list("(, 1)").failed, 1u);   // empty first element
  EXPECT_EQ(recover_list("(1, x)").failed, 1u);  // bad item right before the closer
  const auto trailing = recover_list("(1, 2,)", true);
  EXPECT_TRUE(trailing.ok);
  EXPECT_EQ(trailing.failed, 0u);
  EXPECT_EQ(recover_list("(1, 2,)").failed, 1u); // no trailing allowed: the empty last element fails
}

TEST(ListRecovery, UnrecoverableShapesAreReturnedAndNotRecorded) {
  const auto no_close = recover_list("(1, x");
  EXPECT_FALSE(no_close.ok);
  EXPECT_EQ(no_close.message, "expected separator or closing bracket");
  EXPECT_EQ(no_close.errors.size(), 1u); // only the item error; the shape error is the caller's to report

  const auto no_open = recover_list("1, 2)");
  EXPECT_FALSE(no_open.ok);
  EXPECT_EQ(no_open.message, "expected opening bracket");
  EXPECT_TRUE(no_open.errors.empty());
}

TEST(ListRecovery, TerminatesOnHostileInputAndCapsStoredErrors) {
  EXPECT_FALSE(recover_list("((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((").ok);
  EXPECT_FALSE(recover_list("(,,,,,,,,,,,,,,,,,,,,,,,,").ok); // never closed
  const auto many = recover_list("(x, x, x, x, x, x, x, x)");
  ASSERT_TRUE(many.ok);
  EXPECT_EQ(many.failed, 8u);
  EXPECT_EQ(many.errors.size(), 4u); // capacity
  EXPECT_TRUE(many.errors.overflowed());
}

namespace {

using text = std::string;
using text_value = reloco::expected<text, parse_error>;

// A := name ( "." name | "[" number "]" )*, building a string that shows the order of application.
text_value left_rec(string_view source, size_t max_steps = static_cast<size_t>(-1)) {
  lexer_rules rules;
  rules.punctuation = ".[]";
  token_stream ts{source, rules};
  auto seed = [](token_stream &s) -> text_value {
    auto name = s.expect(token_kind::identifier, "expected a name");
    if (!name) {
      return reloco::unexpected<parse_error>(name.error());
    }
    return text{name.value().text.data(), name.value().text.size()};
  };
  auto extend = [](token_stream &s, text &left) -> reloco::expected<bool, parse_error> {
    if (s.skip(token_kind::punctuation, ".")) {
      auto name = s.expect(token_kind::identifier, "expected a member name");
      if (!name) {
        return reloco::unexpected<parse_error>(name.error());
      }
      left = "(" + left + " ." + text{name.value().text.data(), name.value().text.size()} + ")";
      return true;
    }
    if (s.skip(token_kind::punctuation, "[")) {
      auto index = s.expect(token_kind::integer, "expected an index");
      if (!index) {
        return reloco::unexpected<parse_error>(index.error());
      }
      if (auto close = s.expect(token_kind::punctuation, "]", "expected ']'"); !close) {
        return reloco::unexpected<parse_error>(close.error());
      }
      left = "(" + left + " [" + text{index.value().text.data(), index.value().text.size()} + "])";
      return true;
    }
    return false;
  };
  return microfmt::parse_left_recursive<text>(ts, seed, extend, max_steps);
}

} // namespace

TEST(LeftRecursion, AppliesSuffixesOfDifferentShapesLeftToRight) {
  EXPECT_EQ(left_rec("a").value(), "a");
  EXPECT_EQ(left_rec("a.b[2].c").value(), "(((a .b) [2]) .c)");
}

TEST(LeftRecursion, MalformedSuffixesReportTheirOwnError) {
  auto dangling = left_rec("a.");
  ASSERT_FALSE(dangling.has_value());
  EXPECT_EQ(dangling.error().message, "expected a member name");
  EXPECT_EQ(dangling.error().at.kind, token_kind::end);

  auto unclosed = left_rec("a.b[1");
  ASSERT_FALSE(unclosed.has_value());
  EXPECT_EQ(unclosed.error().message, "expected ']'");

  EXPECT_EQ(left_rec("1").error().message, "expected a name"); // seed failure propagates
}

TEST(LeftRecursion, GuardsAgainstNoProgressAndTooManySuffixes) {
  token_stream ts{"a b", lexer_rules{}};
  auto seed = [](token_stream &s) -> reloco::expected<int, parse_error> {
    (void)s.next();
    return 0;
  };
  auto stuck = [](token_stream &, int &) -> reloco::expected<bool, parse_error> { return true; };
  auto no_progress = microfmt::parse_left_recursive<int>(ts, seed, stuck);
  ASSERT_FALSE(no_progress.has_value());
  EXPECT_EQ(no_progress.error().message, "left recursion made no progress");

  EXPECT_EQ(left_rec("a.b.c.d", 3).value(), "(((a .b) .c) .d)"); // exactly 3 suffixes is allowed
  auto too_many = left_rec("a.b.c.d.e", 3);
  ASSERT_FALSE(too_many.has_value());
  EXPECT_EQ(too_many.error().message, "too many suffixes");
}

TEST(LeftRecursion, WorksWithMoveOnlyValuesAndLongInput) {
  auto seed = [](token_stream &s) -> reloco::expected<std::unique_ptr<int>, parse_error> {
    (void)s.next();
    return std::make_unique<int>(0);
  };
  auto extend = [](token_stream &s, std::unique_ptr<int> &left) -> reloco::expected<bool, parse_error> {
    if (!s.skip(token_kind::punctuation, ".")) {
      return false;
    }
    left = std::make_unique<int>(*left + 1);
    return true;
  };
  lexer_rules rules;
  rules.punctuation = ".";
  token_stream dots{"x . . . . . . . . . . . . . . . . . . . .", rules};
  auto result = microfmt::parse_left_recursive<std::unique_ptr<int>>(dots, seed, extend);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result.value(), 20);
}

namespace {

// A := "(" A ")" "!" | "(" A ")" "?" | "x"   (value: nesting depth). Both parenthesised alternatives
// re-parse the inner A, which is exponential without memoization.
struct nested_grammar {
  using result = reloco::expected<int, parse_error>;

  bool use_memo{false};
  size_t body_runs{0};
  microfmt::memo_table<int, 64> memo{};

  result group(token_stream &ts) {
    if (use_memo) {
      return microfmt::memoize(ts, memo, 1, [this](token_stream &s) { return body(s); });
    }
    return body(ts);
  }

  result wrapped(token_stream &ts, string_view suffix) {
    if (auto open = ts.expect(token_kind::punctuation, "(", "expected '('"); !open) {
      return reloco::unexpected<parse_error>(open.error());
    }
    auto inner = group(ts);
    if (!inner) {
      return inner;
    }
    if (auto close = ts.expect(token_kind::punctuation, ")", "expected ')'"); !close) {
      return reloco::unexpected<parse_error>(close.error());
    }
    if (auto tail = ts.expect(token_kind::op, suffix, "expected a suffix"); !tail) {
      return reloco::unexpected<parse_error>(tail.error());
    }
    return inner.value() + 1;
  }

  result body(token_stream &ts) {
    ++body_runs;
    return microfmt::parse_choice(
        ts, [this](token_stream &s) -> result { return wrapped(s, "!"); },
        [this](token_stream &s) -> result { return wrapped(s, "?"); },
        [](token_stream &s) -> result {
          if (auto x = s.expect(token_kind::identifier, "x", "expected 'x'"); !x) {
            return reloco::unexpected<parse_error>(x.error());
          }
          return 0;
        });
  }
};

constexpr string_view kSuffixOps[] = {"!", "?"};

nested_grammar::result run_nested(nested_grammar &g, string_view text) {
  lexer_rules rules;
  rules.operators = kSuffixOps;
  token_stream ts{text, rules};
  auto result = g.group(ts);
  if (result && !ts.at_end()) {
    return reloco::unexpected<parse_error>(ts.error_here("trailing input"));
  }
  return result;
}

} // namespace

TEST(Packrat, GivesTheSameResultButDoesLinearInsteadOfExponentialWork) {
  constexpr string_view input = "((((((((((((x)?)?)?)?)?)?)?)?)?)?)?)?"; // depth 12, only the "?" alternative matches
  nested_grammar plain;
  auto expected = run_nested(plain, input);
  ASSERT_TRUE(expected.has_value());
  EXPECT_EQ(expected.value(), 12);

  nested_grammar cached;
  cached.use_memo = true;
  auto got = run_nested(cached, input);
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got.value(), 12);

  EXPECT_GT(plain.body_runs, 4000u); // roughly 2^12
  EXPECT_LT(cached.body_runs, 40u);  // one run per nesting level plus the leaf
  EXPECT_GT(cached.memo.hits(), 0u);
  EXPECT_EQ(cached.memo.misses(), cached.body_runs);
}

TEST(Packrat, ErrorsAreCachedAndTheStreamIsRestoredAfterAHit) {
  nested_grammar g;
  g.use_memo = true;
  lexer_rules rules;
  rules.operators = kSuffixOps;
  token_stream ts{"(x) y", rules};

  auto first = g.group(ts); // "(x)" has no suffix, so the whole rule fails at "y"
  ASSERT_FALSE(first.has_value());
  const size_t runs = g.body_runs;
  const size_t offset_after_first = ts.peek().offset;

  token_stream again{"(x) y", rules};
  auto second = g.group(again); // a fresh stream: same position 0, so the cached outcome is replayed
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error().message, first.error().message);
  EXPECT_EQ(g.body_runs, runs); // nothing re-ran
  EXPECT_EQ(again.peek().offset, offset_after_first);
  EXPECT_EQ(again.previous().offset, ts.previous().offset);
}

TEST(Packrat, CollisionsOnlyCostSpeedAndClearForgetsEverything) {
  microfmt::memo_table<int, 1> tiny; // every key collides
  token_stream ts{"x y", lexer_rules{}};
  size_t runs = 0;
  auto rule = [&runs](token_stream &s) -> reloco::expected<int, parse_error> {
    ++runs;
    (void)s.next();
    return 7;
  };
  EXPECT_EQ(microfmt::memoize(ts, tiny, 1, rule).value(), 7);
  EXPECT_EQ(microfmt::memoize(ts, tiny, 2, rule).value(), 7); // different rule and offset: evicts the first
  token_stream back{"x y", lexer_rules{}};
  EXPECT_EQ(microfmt::memoize(back, tiny, 1, rule).value(), 7); // evicted, so it runs again
  EXPECT_EQ(runs, 3u);
  EXPECT_EQ(tiny.hits(), 0u);

  token_stream hit{"x y", lexer_rules{}};
  EXPECT_EQ(microfmt::memoize(hit, tiny, 1, rule).value(), 7);
  EXPECT_EQ(tiny.hits(), 1u);
  EXPECT_EQ(runs, 3u);
  tiny.clear();
  EXPECT_EQ(tiny.hits(), 0u);
  token_stream after{"x y", lexer_rules{}};
  EXPECT_EQ(microfmt::memoize(after, tiny, 1, rule).value(), 7);
  EXPECT_EQ(runs, 4u);
}

TEST(Packrat, TwoRulesShareATableThroughDifferentIds) {
  microfmt::memo_table<int, 16> table;
  token_stream ts{"x", lexer_rules{}};
  auto one = [](token_stream &) -> reloco::expected<int, parse_error> { return 1; };
  auto two = [](token_stream &) -> reloco::expected<int, parse_error> { return 2; };
  EXPECT_EQ(microfmt::memoize(ts, table, 1, one).value(), 1);
  EXPECT_EQ(microfmt::memoize(ts, table, 2, two).value(), 2); // same offset, different id: not a hit
  EXPECT_EQ(microfmt::memoize(ts, table, 1, two).value(), 1); // id 1 still returns rule one's result
  EXPECT_EQ(table.hits(), 1u);
}

TEST(LexerModes, WithRulesChangesTokenizationInsideAndRestoresAfter) {
  const lexer_rules normal;
  lexer_rules quote_is_punctuation;
  quote_is_punctuation.quotes = "";
  quote_is_punctuation.punctuation = "'";

  token_stream ts{"x 'b'", normal};
  EXPECT_EQ(ts.peek2().kind, token_kind::string);

  const auto inside = microfmt::with_rules(ts, quote_is_punctuation, [](token_stream &s) {
    return std::make_pair(s.peek2().kind, s.peek2().text == "'");
  });
  EXPECT_EQ(inside.first, token_kind::punctuation);
  EXPECT_TRUE(inside.second);
  EXPECT_EQ(ts.peek2().kind, token_kind::string);
  EXPECT_EQ(ts.peek2().text, "'b'");
  EXPECT_EQ(ts.peek().text, "x");
}

TEST(LexerModes, RulesAreRestoredWhenTheRuleFailsAndNestingUnwinds) {
  const lexer_rules normal;
  lexer_rules outer;
  outer.quotes = "";
  outer.punctuation = "'";
  lexer_rules inner;
  inner.punctuation = "";

  token_stream ts{"x 'b'", normal};
  auto failed = microfmt::with_rules(ts, outer, [&](token_stream &s) -> reloco::expected<void, parse_error> {
    const auto nested = microfmt::with_rules(s, inner, [](token_stream &t) { return t.peek2().kind; });
    EXPECT_EQ(nested, token_kind::string);
    EXPECT_EQ(s.peek2().kind, token_kind::punctuation);
    return reloco::unexpected<parse_error>(s.error_here("boom"));
  });
  EXPECT_FALSE(failed);
  EXPECT_EQ(ts.peek2().kind, token_kind::string);
}

TEST(LexerModes, BacktrackingRestoresRulesAndPosition) {
  const lexer_rules normal;
  lexer_rules quote_is_punctuation;
  quote_is_punctuation.quotes = "";
  quote_is_punctuation.punctuation = "'";

  token_stream ts{"x 'b'", normal};
  auto failed = microfmt::attempt(ts, [&](token_stream &s) -> reloco::expected<void, parse_error> {
    s.set_rules(quote_is_punctuation);
    (void)s.next();
    return reloco::unexpected<parse_error>(s.error_here("no"));
  });
  EXPECT_FALSE(failed);
  EXPECT_EQ(ts.peek().text, "x");
  EXPECT_EQ(ts.peek2().kind, token_kind::string);
}

TEST(LexerModes, TakeRawKeepsWhitespaceStopsAtStopBytesAndTracksLines) {
  const lexer_rules rules;
  token_stream ts{"(  a b\n c ) d", rules};
  ASSERT_TRUE(ts.accept(token_kind::punctuation));
  const microfmt::token raw = ts.take_raw_until(")");
  EXPECT_EQ(raw.kind, token_kind::string);
  EXPECT_EQ(raw.text, "  a b\n c ");
  EXPECT_EQ(raw.line, 1u);
  EXPECT_EQ(ts.peek().text, ")");
  EXPECT_EQ(ts.peek().line, 2u);
  EXPECT_EQ(ts.peek().column, 4u);
  EXPECT_EQ(ts.peek2().text, "d");

  const microfmt::token empty = ts.take_raw_until(")");
  EXPECT_TRUE(empty.text.empty());
  ASSERT_TRUE(ts.accept(token_kind::punctuation));
  const microfmt::token rest = ts.take_raw_until("");
  EXPECT_EQ(rest.text, " d");
  EXPECT_TRUE(ts.at_end());
  EXPECT_TRUE(ts.take_raw_until(")").text.empty());
}

namespace {

// template := '"' ( raw | '${' ident '}' )* '"'; the pieces are joined with '|' for the assertions.
reloco::expected<void, parse_error> parse_template(token_stream &ts, std::string &pieces) {
  if (auto open = ts.expect(token_kind::punctuation, "\"", "expected '\"'"); !open) {
    return reloco::unexpected<parse_error>(open.error());
  }
  for (;;) {
    const microfmt::token raw = ts.take_raw_until("\"$");
    if (!raw.text.empty()) {
      pieces += "T(" + std::string(raw.text.data(), raw.text.size()) + ")|";
    }
    if (ts.check(token_kind::punctuation, "\"")) {
      (void)ts.next();
      return {};
    }
    if (auto dollar = ts.expect(token_kind::punctuation, "$", "unterminated string"); !dollar) {
      return reloco::unexpected<parse_error>(dollar.error());
    }
    if (auto open = ts.expect(token_kind::punctuation, "{", "expected '{' after '$'"); !open) {
      return reloco::unexpected<parse_error>(open.error());
    }
    auto name = ts.expect(token_kind::identifier, "expected a name");
    if (!name) {
      return reloco::unexpected<parse_error>(name.error());
    }
    pieces += "E(" + std::string(name->text.data(), name->text.size()) + ")|";
    if (auto close = ts.expect(token_kind::punctuation, "}", "expected '}'"); !close) {
      return reloco::unexpected<parse_error>(close.error());
    }
  }
}

} // namespace

TEST(LexerModes, InterpolatedStringIsParsedByTheParser) {
  const lexer_rules normal;
  lexer_rules tmpl;
  tmpl.quotes = "";
  tmpl.punctuation = "\"${}";

  std::string pieces;
  token_stream ts{"\"a ${x} b ${yy}!\" tail", normal};
  const auto ok = microfmt::with_rules(ts, tmpl, [&](token_stream &s) { return parse_template(s, pieces); });
  ASSERT_TRUE(ok);
  EXPECT_EQ(pieces, "T(a )|E(x)|T( b )|E(yy)|T(!)|");
  EXPECT_EQ(ts.peek().text, "tail");

  std::string bad_pieces;
  token_stream bad{"\"a ${x\n", normal};
  const auto failed = microfmt::with_rules(bad, tmpl, [&](token_stream &s) { return parse_template(s, bad_pieces); });
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error().at.line, 2u);
}

namespace {

struct pool_node {
  int value{0};
  char op{0};
  microfmt::node_id<pool_node> left{};
  microfmt::node_id<pool_node> right{};
};
using pool_id = microfmt::node_id<pool_node>;
using pool_result = reloco::expected<pool_id, parse_error>;

template <size_t N> struct tree_actions {
  microfmt::node_pool<pool_node, N> &pool;

  pool_result primary(token_stream &s) {
    if (!s.check(token_kind::integer)) {
      return reloco::unexpected<parse_error>(s.error_here("expected a number"));
    }
    const microfmt::token tok = s.next();
    return pool.make(tok, pool_node{static_cast<int>(microfmt::parse<int64_t>(tok.text).value()), 0, {}, {}});
  }
  pool_result make_binary(const binary_op &op, pool_id l, pool_id r) {
    return pool.make(microfmt::token{}, pool_node{0, op.text[0], l, r});
  }
};

int evaluate(const microfmt::node_pool<pool_node, 8> &pool, pool_id id) {
  const auto node_ref = pool.get(id);
  const pool_node &node = *node_ref;
  if (node.op == 0) {
    return node.value;
  }
  const int l = evaluate(pool, node.left);
  const int r = evaluate(pool, node.right);
  return node.op == '+' ? l + r : l * r;
}

} // namespace

TEST(NodePool, MakesNodesAndChecksHandles) {
  microfmt::node_pool<pool_node, 2> pool;
  auto a = pool.make(microfmt::token{}, pool_node{7, 0, {}, {}});
  auto b = pool.make(microfmt::token{}, pool_node{8, 0, {}, {}});
  auto c = pool.make(microfmt::token{}, pool_node{9, 0, {}, {}}); // spills past the inline capacity
  ASSERT_TRUE(a && b && c);
  EXPECT_EQ(a.value().index, 0u);
  EXPECT_EQ(c.value().index, 2u);
  EXPECT_EQ(pool.size(), 3u);
  EXPECT_EQ(pool.get(b.value())->value, 8);
  EXPECT_EQ(pool.try_get(a.value())->value, 7); // handles stay valid after the pool grew
  EXPECT_FALSE(pool.try_get(pool_id::invalid()));
  EXPECT_FALSE(pool.try_get(pool_id{99}));
  EXPECT_FALSE(pool_id::invalid().valid());
}

TEST(NodePool, FullPoolReportsAnErrorAtTheGivenToken) {
  microfmt::node_pool<pool_node, 2> pool{2};
  token_stream ts{"x y z", lexer_rules{}};
  ASSERT_TRUE(pool.make(ts.next(), pool_node{}));
  ASSERT_TRUE(pool.make(ts.next(), pool_node{}));
  const microfmt::token third = ts.next();
  auto full = pool.make(third, pool_node{});
  ASSERT_FALSE(full);
  EXPECT_EQ(full.error().message, "node pool full");
  EXPECT_EQ(full.error().at.text, "z");
  EXPECT_EQ(pool.size(), 2u);
}

TEST(NodePool, RollbackDropsLaterNodesOnly) {
  microfmt::node_pool<pool_node, 4> pool;
  auto keep = pool.make(microfmt::token{}, pool_node{1, 0, {}, {}});
  const microfmt::pool_mark m = pool.mark();
  auto drop = pool.make(microfmt::token{}, pool_node{2, 0, {}, {}});
  pool.rollback(m);
  EXPECT_EQ(pool.size(), 1u);
  EXPECT_TRUE(pool.try_get(keep.value()));
  EXPECT_FALSE(pool.try_get(drop.value()));
  pool.rollback(microfmt::pool_mark{10}); // larger than the pool: no effect
  EXPECT_EQ(pool.size(), 1u);
  pool.clear();
  EXPECT_EQ(pool.size(), 0u);
}

TEST(NodePool, PooledRollsBackFailedAlternativesInsideChoice) {
  microfmt::node_pool<pool_node, 8> pool;
  lexer_rules rules;
  rules.operators = kCalcOps;
  token_stream ts{"1 + 2 * 3 ;", rules};
  const microfmt::expression_grammar grammar{kMixfixBinary, {}, {}, {}, {}};

  // First alternative builds the whole expression, then fails on the missing '!'; the second succeeds.
  auto with_bang = microfmt::pooled(pool, [&](token_stream &s) -> pool_result {
    tree_actions<8> actions{pool};
    auto tree = microfmt::parse_expression<pool_id>(s, grammar, actions);
    if (!tree) {
      return tree;
    }
    if (auto bang = s.expect(token_kind::punctuation, "!", "expected '!'"); !bang) {
      return reloco::unexpected<parse_error>(bang.error());
    }
    return tree;
  });
  auto with_semicolon = microfmt::pooled(pool, [&](token_stream &s) -> pool_result {
    tree_actions<8> actions{pool};
    auto tree = microfmt::parse_expression<pool_id>(s, grammar, actions);
    if (!tree) {
      return tree;
    }
    if (auto semi = s.expect(token_kind::punctuation, ";", "expected ';'"); !semi) {
      return reloco::unexpected<parse_error>(semi.error());
    }
    return tree;
  });

  auto result = microfmt::parse_choice(ts, with_bang, with_semicolon);
  ASSERT_TRUE(result);
  EXPECT_EQ(pool.size(), 5u); // 3 numbers + 2 operators, none left over from the failed alternative
  EXPECT_EQ(evaluate(pool, result.value()), 7);
}

TEST(NodePool, FullPoolStopsTheExpressionWithAnError) {
  microfmt::node_pool<pool_node, 2> pool{3};
  lexer_rules rules;
  rules.operators = kCalcOps;
  token_stream ts{"1 + 2 + 3", rules};
  tree_actions<2> actions{pool};
  const microfmt::expression_grammar grammar{kMixfixBinary, {}, {}, {}, {}};
  auto result = microfmt::parse_expression<pool_id>(ts, grammar, actions);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().message, "node pool full");
}

TEST(StringPool, InternsEqualTextToEqualIds) {
  microfmt::string_pool<16> pool;
  const microfmt::token at{};
  auto a = pool.intern("alpha", at);
  auto b = pool.intern("beta", at);
  auto again = pool.intern("alpha", at);
  auto empty = pool.intern("", at);
  ASSERT_TRUE(a && b && again && empty);
  EXPECT_EQ(a.value(), again.value());
  EXPECT_NE(a.value(), b.value());
  EXPECT_NE(pool.intern("Alpha", at).value(), a.value()); // case sensitive
  EXPECT_EQ(pool.view(a.value()), "alpha");
  EXPECT_EQ(pool.view(empty.value()), "");
  EXPECT_EQ(pool.intern("", at).value(), empty.value());
  EXPECT_EQ(pool.size(), 4u);
  EXPECT_EQ(pool.bytes(), 14u); // "alpha" "beta" "Alpha"; the empty string adds none

  // A token interns its own text.
  token_stream ts{"gamma", lexer_rules{}};
  const microfmt::token word = ts.next();
  auto g = pool.intern(word);
  ASSERT_TRUE(g);
  EXPECT_EQ(pool.view(g.value()), "gamma");
}

TEST(StringPool, FindNeverInsertsAndBadIdsGiveEmptyViews) {
  microfmt::string_pool<16> pool;
  const microfmt::token at{};
  auto x = pool.intern("x", at);
  EXPECT_EQ(pool.find("x"), x.value());
  EXPECT_FALSE(pool.find("y").valid());
  EXPECT_EQ(pool.size(), 1u);
  EXPECT_EQ(pool.view(microfmt::string_id::invalid()), "");
  EXPECT_EQ(pool.view(microfmt::string_id{42}), "");
  const microfmt::string_pool<16> untouched;
  EXPECT_FALSE(untouched.find("x").valid());
}

TEST(StringPool, ManyStringsKeepWorkingAcrossGrowth) {
  microfmt::string_pool<16> pool; // far more strings than the inline capacity and the initial bucket count
  const microfmt::token at{};
  reloco::sso_vector<microfmt::string_id, 8> ids;
  for (int i = 0; i < 3000; ++i) {
    const std::string name = "name_" + std::to_string(i);
    auto id = pool.intern(microfmt::string_view(name.data(), name.size()), at);
    ASSERT_TRUE(id);
    ASSERT_TRUE(ids.try_push_back(id.value()));
  }
  EXPECT_EQ(pool.size(), 3000u);
  for (int i = 0; i < 3000; ++i) {
    const std::string name = "name_" + std::to_string(i);
    const microfmt::string_view text(name.data(), name.size());
    EXPECT_EQ(pool.find(text), ids[static_cast<size_t>(i)]);
    EXPECT_EQ(pool.view(ids[static_cast<size_t>(i)]), text);
  }
}

TEST(StringPool, LimitsReportAnErrorAtTheGivenToken) {
  const microfmt::token at{};
  microfmt::string_pool<16> few{1000, 2};
  ASSERT_TRUE(few.intern("a", at) && few.intern("b", at));
  auto third = few.intern("c", at);
  ASSERT_FALSE(third);
  EXPECT_EQ(third.error().message, "string pool full");
  EXPECT_TRUE(few.intern("a", at)); // an existing string needs no room

  microfmt::string_pool<16> small{4, 100};
  ASSERT_TRUE(small.intern("abcd", at));
  EXPECT_FALSE(small.intern("e", at));
  EXPECT_EQ(small.size(), 1u);
  EXPECT_EQ(small.bytes(), 4u);
}

TEST(StringPool, RollbackDropsLaterStringsAndAllowsReuse) {
  microfmt::string_pool<8> pool;
  const microfmt::token at{};
  auto keep = pool.intern("keep", at);
  const microfmt::string_pool_mark m = pool.mark();
  for (int i = 0; i < 100; ++i) { // enough to force growth after the mark
    const std::string name = "tmp" + std::to_string(i);
    ASSERT_TRUE(pool.intern(microfmt::string_view(name.data(), name.size()), at));
  }
  pool.rollback(m);
  EXPECT_EQ(pool.size(), 1u);
  EXPECT_EQ(pool.bytes(), 4u);
  EXPECT_FALSE(pool.find("tmp5").valid());
  EXPECT_EQ(pool.find("keep"), keep.value());
  auto again = pool.intern("tmp5", at); // the freed index is reused
  ASSERT_TRUE(again);
  EXPECT_EQ(again.value().index, 1u);
  EXPECT_EQ(pool.view(again.value()), "tmp5");
  pool.clear();
  EXPECT_EQ(pool.size(), 0u);
  EXPECT_FALSE(pool.find("keep").valid());
  EXPECT_TRUE(pool.intern("keep", at));
}

TEST(StringPool, InternAllGivesKeywordsTheFirstIds) {
  microfmt::string_pool<32> pool;
  const microfmt::token at{};
  const microfmt::string_view words[] = {"let", "print", "if"};
  ASSERT_TRUE(pool.intern_all(reloco::span<const microfmt::string_view>(words), at));
  EXPECT_EQ(pool.find("let").index, 0u);
  EXPECT_EQ(pool.find("if").index, 2u);
  EXPECT_GE(pool.intern("counter", at).value().index, 3u);
}

TEST(StringPool, PooledRollsBackFailedAlternatives) {
  microfmt::string_pool<32> pool;
  token_stream ts{"abc ;", lexer_rules{}};
  auto with_bang = microfmt::pooled(pool, [&](token_stream &s) -> reloco::expected<microfmt::string_id, parse_error> {
    auto word = s.expect(token_kind::identifier, "expected a name");
    if (!word) {
      return reloco::unexpected<parse_error>(word.error());
    }
    auto id = pool.intern(word.value());
    if (!id) {
      return id;
    }
    if (auto bang = s.expect(token_kind::punctuation, "!", "expected '!'"); !bang) {
      return reloco::unexpected<parse_error>(bang.error());
    }
    return id;
  });
  auto with_semicolon = [&](token_stream &s) -> reloco::expected<microfmt::string_id, parse_error> {
    auto word = s.expect(token_kind::identifier, "expected a name");
    if (!word) {
      return reloco::unexpected<parse_error>(word.error());
    }
    return pool.intern(word.value());
  };
  auto result = microfmt::parse_choice(ts, with_bang, with_semicolon);
  ASSERT_TRUE(result);
  EXPECT_EQ(pool.size(), 1u); // "abc" was interned once by the second alternative; the failed one left nothing
  EXPECT_EQ(result.value().index, 0u);
}

TEST(StringPool, BuilderDecodesTextPieceByPiece) {
  microfmt::string_pool<16> pool;
  const microfmt::token at{};
  auto plain = pool.intern("a\nb", at);
  {
    auto b = pool.begin();
    b.append("a");
    b.push_back('\n');
    b.append("b");
    EXPECT_EQ(b.view(), "a\nb");
    auto id = b.finish(at);
    ASSERT_TRUE(id);
    EXPECT_EQ(id.value(), plain.value()); // an equal string is shared, the copy is dropped
    EXPECT_EQ(pool.size(), 1u);
    EXPECT_EQ(pool.bytes(), 3u);
  }
  auto b = pool.begin();
  b.append("new");
  auto fresh = b.finish(at);
  ASSERT_TRUE(fresh);
  EXPECT_EQ(pool.view(fresh.value()), "new");
  EXPECT_EQ(pool.size(), 2u);
}

TEST(StringPool, BuilderRulesBusyAbandonAndLimits) {
  const microfmt::token at{};
  microfmt::string_pool<16> pool{8, 100};
  ASSERT_TRUE(pool.intern("base", at));
  {
    auto b = pool.begin();
    b.append("tmp");
    auto busy = pool.intern("x", at); // one writer at a time
    ASSERT_FALSE(busy);
    EXPECT_EQ(busy.error().message, "string pool busy");
    auto second = pool.begin();
    EXPECT_FALSE(second.finish(at));
    pool.clear(); // ignored while a builder is open
    EXPECT_EQ(pool.size(), 1u);
  } // destructor abandons the text
  EXPECT_EQ(pool.bytes(), 4u);
  EXPECT_TRUE(pool.intern("x", at));

  auto b = pool.begin();
  b.append("abcd"); // 4 + 1 + 4 > 8: the failure is remembered
  b.append("e");
  auto failed = b.finish(at);
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error().message, "string pool full");
  EXPECT_EQ(pool.bytes(), 5u);
  EXPECT_TRUE(pool.intern("ok", at)); // the pool is usable again
}

TEST(Recovery, CollectsSeveralErrorsAndTerminates) {
  // statement := name ';'
  token_stream ts{"a; b b; c d; e;", lexer_rules{}};
  constexpr microfmt::token_sync kSemicolon[] = {{token_kind::punctuation, ";"}};
  size_t good = 0;
  size_t errors = 0;
  while (!ts.at_end()) {
    auto statement = [&ts]() -> reloco::expected<void, parse_error> {
      if (auto name = ts.expect(token_kind::identifier, "expected a name"); !name) {
        return reloco::unexpected<parse_error>(name.error());
      }
      if (auto semi = ts.expect(token_kind::punctuation, ";", "expected ';'"); !semi) {
        return reloco::unexpected<parse_error>(semi.error());
      }
      return {};
    }();
    if (statement) {
      ++good;
      continue;
    }
    ++errors;
    (void)microfmt::recover_to(ts, kSemicolon);
    (void)ts.skip(token_kind::punctuation, ";");
  }
  EXPECT_EQ(good, 2u);
  EXPECT_EQ(errors, 2u);
}

TEST(Recovery, StopsAtSyncTokenWithoutConsumingAndHandlesGarbage) {
  token_stream ts{"x y ; z", lexer_rules{}};
  constexpr microfmt::token_sync kSemicolon[] = {{token_kind::punctuation, ";"}};
  EXPECT_EQ(microfmt::recover_to(ts, kSemicolon), 2u);
  EXPECT_EQ(ts.peek().text, ";");
  EXPECT_EQ(microfmt::recover_to(ts, kSemicolon), 0u); // already at a sync token

  token_stream garbage{"@ @ @", lexer_rules{}};
  EXPECT_EQ(microfmt::recover_to(garbage, kSemicolon), 3u); // no sync token: runs to the end
  EXPECT_TRUE(garbage.at_end());

  token_stream any_kind{"a 1 b", lexer_rules{}};
  constexpr microfmt::token_sync kInteger[] = {{token_kind::integer, {}}};
  EXPECT_EQ(microfmt::recover_to(any_kind, kInteger), 1u);
}

namespace {

constexpr string_view kTernaryOps[] = {"+", "?", ":"};

template <typename T> microfmt::span<const T> span_none() { return {}; }

calc_result calc_ternary(string_view text) {
  lexer_rules rules;
  rules.operators = kTernaryOps;
  token_stream ts{text, rules};
  auto primary = [](token_stream &s) -> calc_result {
    if (!s.check(token_kind::integer)) {
      return reloco::unexpected<parse_error>(s.error_here("expected a number"));
    }
    return microfmt::parse<int64_t>(s.next().text).value();
  };
  auto binary = [](const binary_op &, int64_t l, int64_t r) -> calc_result { return l + r; };
  auto prefix = [](const prefix_op &, int64_t v) -> calc_result { return v; };
  auto postfix = [](const microfmt::postfix_op &, int64_t v, token_stream &) -> calc_result { return v; };
  auto ternary = [](const microfmt::ternary_op &, int64_t c, int64_t t, int64_t e) -> calc_result {
    return c != 0 ? t : e;
  };
  constexpr binary_op plus[] = {{"+", 10}};
  return microfmt::parse_expression<int64_t>(ts, plus, span_none<prefix_op>(), span_none<microfmt::postfix_op>(),
                                             microfmt::ternary_op{}, primary, binary, prefix, postfix, ternary);
}

} // namespace

TEST(Expression, TernarySelectsAndNestsToTheRight) {
  EXPECT_EQ(calc_ternary("1 ? 10 : 20").value(), 10);
  EXPECT_EQ(calc_ternary("0 ? 10 : 20").value(), 20);
  EXPECT_EQ(calc_ternary("0 ? 1 : 0 ? 2 : 3").value(), 3); // a ? b : (c ? d : e)
  EXPECT_EQ(calc_ternary("1 ? 0 ? 1 : 2 : 3").value(), 2); // nested in the middle operand
  EXPECT_EQ(calc_ternary("1 + 0 ? 5 : 6").value(), 5);     // binary binds tighter: (1 + 0) ? 5 : 6
  EXPECT_EQ(calc_ternary("0 ? 5 : 1 + 1").value(), 2);     // else part is a full binary expression
}

TEST(Expression, TernaryReportsMissingColon) {
  auto r = calc_ternary("1 ? 2 3");
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().message, "expected ':'");
  EXPECT_EQ(r.error().at.column, 7u);
  EXPECT_FALSE(calc_ternary("1 ? 2").has_value());
  EXPECT_FALSE(calc_ternary("1 ? 2 :").has_value());
}

TEST(ErrorList, StoresUpToCapacityAndCountsTheRest) {
  microfmt::error_list<2> list;
  EXPECT_TRUE(list.empty());
  list.add(parse_error{"one", {}});
  list.add(parse_error{"two", {}});
  list.add(parse_error{"three", {}});
  EXPECT_FALSE(list.empty());
  EXPECT_EQ(list.size(), 2u);
  EXPECT_EQ(list.total(), 3u);
  EXPECT_TRUE(list.overflowed());
  const auto stored = list.errors();
  ASSERT_EQ(stored.size(), 2u);
  EXPECT_EQ(stored[0].message, "one");
  EXPECT_EQ(stored[1].message, "two");
}

namespace {

constexpr microfmt::token_sync kStatementEnd[] = {{token_kind::punctuation, ";"}};

// statement := identifier ';'
reloco::expected<void, parse_error> name_statement(token_stream &ts) {
  if (auto name = ts.expect(token_kind::identifier, "expected a name"); !name) {
    return reloco::unexpected<parse_error>(name.error());
  }
  if (auto semi = ts.expect(token_kind::punctuation, ";", "expected ';'"); !semi) {
    return reloco::unexpected<parse_error>(semi.error());
  }
  return {};
}

} // namespace

TEST(ParseStatements, CollectsEveryErrorAndCountsGoodStatements) {
  token_stream ts{"a; b b; c d; e;", lexer_rules{}};
  microfmt::error_list<8> errors;
  EXPECT_EQ(microfmt::parse_statements(ts, kStatementEnd, errors, name_statement), 2u);
  const auto stored = errors.errors();
  ASSERT_EQ(stored.size(), 2u);
  EXPECT_EQ(stored[0].message, "expected ';'");
  EXPECT_EQ(stored[0].at.column, 6u);
  EXPECT_EQ(stored[1].at.column, 11u);
}

TEST(ParseStatements, HandlesNoErrorsNoSyncTokenAndEmptyInput) {
  microfmt::error_list<4> none;
  token_stream ok{"a; b;", lexer_rules{}};
  EXPECT_EQ(microfmt::parse_statements(ok, kStatementEnd, none, name_statement), 2u);
  EXPECT_TRUE(none.empty());

  token_stream empty{"", lexer_rules{}};
  EXPECT_EQ(microfmt::parse_statements(empty, kStatementEnd, none, name_statement), 0u);

  microfmt::error_list<4> errors;
  token_stream unterminated{"a; 1 2 3", lexer_rules{}}; // error, and no ';' before the end
  EXPECT_EQ(microfmt::parse_statements(unterminated, kStatementEnd, errors, name_statement), 1u);
  EXPECT_EQ(errors.total(), 1u);
}

TEST(ParseStatements, ErrorOnTheSyncTokenAndGarbageStillTerminate) {
  microfmt::error_list<16> errors;
  token_stream ts{"; ; @ ; a;", lexer_rules{}};
  EXPECT_EQ(microfmt::parse_statements(ts, kStatementEnd, errors, name_statement), 1u);
  EXPECT_EQ(errors.total(), 3u);

  // A statement that succeeds without consuming input must not loop forever.
  token_stream stuck{"x y", lexer_rules{}};
  microfmt::error_list<4> none;
  auto no_progress = [](token_stream &) -> reloco::expected<void, parse_error> { return {}; };
  EXPECT_EQ(microfmt::parse_statements(stuck, kStatementEnd, none, no_progress), 2u);
}

namespace {

reloco::expected<int, parse_error> int_rule(token_stream &ts) {
  auto tok = ts.expect(token_kind::integer, "expected integer");
  if (!tok) {
    return reloco::unexpected<parse_error>(tok.error());
  }
  return 1;
}

// Consumes an identifier then requires an integer: fails one token deeper than int_rule.
reloco::expected<int, parse_error> ident_then_int(token_stream &ts) {
  if (auto id = ts.expect(token_kind::identifier, "expected identifier"); !id) {
    return reloco::unexpected<parse_error>(id.error());
  }
  if (auto n = ts.expect(token_kind::integer, "expected integer after name"); !n) {
    return reloco::unexpected<parse_error>(n.error());
  }
  return 2;
}

} // namespace

TEST(Choice, AttemptRestoresOnFailureAndKeepsOnSuccess) {
  token_stream ts{"a b", lexer_rules{}};
  auto failed = microfmt::attempt(ts, ident_then_int);
  EXPECT_FALSE(failed);
  EXPECT_EQ(ts.peek().text, "a");

  auto ok = microfmt::attempt(ts, [](token_stream &s) -> reloco::expected<int, parse_error> {
    (void)s.accept(token_kind::identifier);
    return 7;
  });
  ASSERT_TRUE(ok);
  EXPECT_EQ(ts.peek().text, "b");
}

TEST(Choice, FirstSuccessWinsAndLaterRulesStartFromTheSamePosition) {
  token_stream ts{"x 5", lexer_rules{}};
  auto r = microfmt::parse_choice(ts, int_rule, ident_then_int);
  ASSERT_TRUE(r);
  EXPECT_EQ(r.value(), 2);
  EXPECT_TRUE(ts.at_end());

  token_stream one{"9", lexer_rules{}};
  auto single = microfmt::parse_choice(one, int_rule);
  ASSERT_TRUE(single);
  EXPECT_EQ(single.value(), 1);
}

TEST(Choice, AllFailingReportsTheFurthestErrorAndRewinds) {
  token_stream ts{"x y", lexer_rules{}};
  auto r = microfmt::parse_choice(ts, int_rule, ident_then_int);
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().message, "expected integer after name");
  EXPECT_EQ(r.error().at.text, "y");
  EXPECT_EQ(ts.peek().text, "x");

  // Equal offsets keep the first alternative's error.
  token_stream tie{"x", lexer_rules{}};
  auto t = microfmt::parse_choice(tie, int_rule, int_rule);
  ASSERT_FALSE(t);
  EXPECT_EQ(t.error().message, "expected integer");
}

TEST(Choice, OptionalIsEmptyPresentOrAnErrorAfterConsumption) {
  token_stream ts{"x", lexer_rules{}};
  auto empty = microfmt::parse_optional(ts, int_rule);
  ASSERT_TRUE(empty);
  EXPECT_FALSE(empty.value().has_value());
  EXPECT_EQ(ts.peek().text, "x");

  token_stream num{"4", lexer_rules{}};
  auto present = microfmt::parse_optional(num, int_rule);
  ASSERT_TRUE(present);
  ASSERT_TRUE(present.value().has_value());
  EXPECT_EQ(*present.value(), 1);

  token_stream partial{"x y", lexer_rules{}};
  auto bad = microfmt::parse_optional(partial, ident_then_int);
  ASSERT_FALSE(bad);
  EXPECT_EQ(bad.error().message, "expected integer after name");
}

TEST(Repeat, CollectsItemsInOrderAndStopsCleanlyOnNoMatch) {
  token_stream ts{"1 2 3 x", lexer_rules{}};
  size_t n = 0;
  auto count = microfmt::parse_repeat(ts, {}, int_rule, [&](int &&) { ++n; });
  ASSERT_TRUE(count);
  EXPECT_EQ(count.value(), 3u);
  EXPECT_EQ(n, 3u);
  EXPECT_EQ(ts.peek().text, "x");

  token_stream none{"x", lexer_rules{}};
  auto zero = microfmt::parse_repeat(none, {}, int_rule, [](int &&) {});
  ASSERT_TRUE(zero);
  EXPECT_EQ(zero.value(), 0u);
}

TEST(Repeat, EnforcesMinMaxAndReportsFailureAfterConsumption) {
  token_stream few{"1 x", lexer_rules{}};
  auto short_run = microfmt::parse_repeat(few, {.min = 2, .too_few_message = "need two"}, int_rule, [](int &&) {});
  ASSERT_FALSE(short_run);
  EXPECT_EQ(short_run.error().message, "need two");

  token_stream many{"1 2 3 4", lexer_rules{}};
  auto capped = microfmt::parse_repeat(many, {.max = 2}, int_rule, [](int &&) {});
  ASSERT_TRUE(capped);
  EXPECT_EQ(capped.value(), 2u);
  EXPECT_EQ(many.peek().text, "3");

  token_stream broken{"x 1 y z", lexer_rules{}};
  auto fail = microfmt::parse_repeat(broken, {}, ident_then_int, [](int &&) {});
  ASSERT_FALSE(fail);
  EXPECT_EQ(fail.error().at.text, "z");
}

TEST(Repeat, NoProgressItemTerminates) {
  token_stream ts{"a b", lexer_rules{}};
  size_t calls = 0;
  auto count = microfmt::parse_repeat(
      ts, {}, [](token_stream &) -> reloco::expected<int, parse_error> { return 1; }, [&](int &&) { ++calls; });
  ASSERT_TRUE(count);
  EXPECT_EQ(count.value(), 0u);
  EXPECT_EQ(calls, 0u);
}

TEST(Repeat, UntilStopsAtTerminatorAndRejectsMissingOrBadItems) {
  token_stream ts{"1 2 ; 9", lexer_rules{}};
  size_t total = 0;
  auto count = microfmt::parse_until(ts, token_kind::punctuation, ";", int_rule, [&](int &&v) { total += size_t(v); });
  ASSERT_TRUE(count);
  EXPECT_EQ(count.value(), 2u);
  EXPECT_EQ(total, 2u);
  EXPECT_EQ(ts.peek().text, ";");

  token_stream open{"1 2", lexer_rules{}};
  auto eof = microfmt::parse_until(open, token_kind::punctuation, ";", int_rule, [](int &&) {});
  ASSERT_FALSE(eof);
  EXPECT_EQ(eof.error().message, "unexpected end of input");

  token_stream bad{"1 x ;", lexer_rules{}};
  auto item_fail = microfmt::parse_until(bad, token_kind::punctuation, ";", int_rule, [](int &&) {});
  ASSERT_FALSE(item_fail);
  EXPECT_EQ(item_fail.error().at.text, "x");

  token_stream stuck{"a ;", lexer_rules{}};
  auto stalled = microfmt::parse_until(
      stuck, token_kind::punctuation, ";", [](token_stream &) -> reloco::expected<int, parse_error> { return 1; },
      [](int &&) {});
  ASSERT_FALSE(stalled);
  EXPECT_EQ(stalled.error().message, "item consumed no input");
}

namespace {

constexpr microfmt::bracket_pair kBraces[] = {{token_kind::punctuation, "{", token_kind::punctuation, "}"}};

} // namespace

TEST(BalancedRecovery, IgnoresSyncTokensInsideNestedBrackets) {
  token_stream ts{"x { a; { b; } c; } ; y", lexer_rules{}};
  EXPECT_EQ(microfmt::recover_balanced(ts, kStatementEnd, kBraces), 11u);
  EXPECT_EQ(ts.peek().text, ";");

  // A stray closer at depth 0 is skipped, not treated as an opener.
  token_stream stray{"} ; z", lexer_rules{}};
  EXPECT_EQ(microfmt::recover_balanced(stray, kStatementEnd, kBraces), 1u);
  EXPECT_EQ(stray.peek().text, ";");
}

TEST(BalancedRecovery, UnmatchedOpenerEndsAtEndOfInputAndDepthIsCapped) {
  token_stream open{"{ a ;", lexer_rules{}};
  EXPECT_EQ(microfmt::recover_balanced(open, kStatementEnd, kBraces), 3u);
  EXPECT_TRUE(open.at_end());

  token_stream deep{"{ { { ;", lexer_rules{}};
  EXPECT_EQ(microfmt::recover_balanced(deep, kStatementEnd, kBraces, 2), 2u);
  EXPECT_EQ(deep.peek().text, "{");
}

TEST(BalancedRecovery, ParseStatementsDoesNotResumeInsideABlock) {
  // The broken header `a b` is followed by a block whose inner ';' used to end recovery early.
  constexpr string_view source = "a b { c; d; } e;";
  microfmt::error_list<8> plain;
  token_stream first{source, lexer_rules{}};
  (void)microfmt::parse_statements(first, kStatementEnd, plain, name_statement);
  EXPECT_GT(plain.total(), 1u);

  microfmt::error_list<8> balanced;
  token_stream second{source, lexer_rules{}};
  EXPECT_EQ(microfmt::parse_statements(second, kStatementEnd, kBraces, balanced, name_statement), 0u);
  EXPECT_EQ(balanced.total(), 1u);
}

TEST(ExpectOneOf, MatchesAnyKindOrReportsTheGivenMessage) {
  constexpr token_kind kinds[] = {token_kind::integer, token_kind::string};
  token_stream ts{"7 \"s\" name", lexer_rules{}};
  auto first = microfmt::expect_one_of(ts, kinds, "expected integer or string");
  ASSERT_TRUE(first);
  EXPECT_EQ(first.value().text, "7");
  EXPECT_TRUE(microfmt::expect_one_of(ts, kinds, "expected integer or string"));

  auto bad = microfmt::expect_one_of(ts, kinds, "expected integer or string");
  ASSERT_FALSE(bad);
  EXPECT_EQ(bad.error().message, "expected integer or string");
  EXPECT_EQ(bad.error().at.text, "name");
  EXPECT_EQ(ts.peek().text, "name");

  EXPECT_FALSE(microfmt::expect_one_of(ts, span_none<token_kind>(), "nothing allowed"));
}

namespace {

// block := identifier '{' (name_statement | block)* '}', collecting body errors into `errors`.
struct block_parser {
  microfmt::error_list<8> &errors;

  reloco::expected<void, parse_error> operator()(token_stream &ts) {
    if (auto name = ts.expect(token_kind::identifier, "expected a name"); !name) {
      return reloco::unexpected<parse_error>(name.error());
    }
    if (auto open = ts.expect(token_kind::punctuation, "{", "expected '{'"); !open) {
      return reloco::unexpected<parse_error>(open.error());
    }
    auto body = [this](token_stream &s) -> reloco::expected<void, parse_error> {
      return microfmt::parse_choice(s, name_statement, *this);
    };
    (void)microfmt::parse_statements_until(ts, token_kind::punctuation, "}", kStatementEnd, kBraces, errors, body);
    if (auto close = ts.expect(token_kind::punctuation, "}", "expected '}'"); !close) {
      return reloco::unexpected<parse_error>(close.error());
    }
    return {};
  }
};

size_t parse_blocks(string_view source, microfmt::error_list<8> &errors) {
  token_stream ts{source, lexer_rules{}};
  block_parser block{errors};
  auto top = [&block](token_stream &s) -> reloco::expected<void, parse_error> {
    return microfmt::parse_choice(s, name_statement, block);
  };
  return microfmt::parse_statements(ts, kStatementEnd, kBraces, errors, top);
}

} // namespace

TEST(BlockRecovery, ErrorsInsideABlockStayInsideAndLaterStatementsParse) {
  microfmt::error_list<8> errors;
  EXPECT_EQ(parse_blocks("net { a b; c; } d;", errors), 2u);
  ASSERT_EQ(errors.total(), 1u);
  const auto stored = errors.errors();
  EXPECT_EQ(stored[0].message, "expected ';'");

  microfmt::error_list<8> two;
  EXPECT_EQ(parse_blocks("net { a b; c d; e; } f;", two), 2u);
  EXPECT_EQ(two.total(), 2u);
}

TEST(BlockRecovery, EmptyAndNestedBlocks) {
  microfmt::error_list<8> errors;
  EXPECT_EQ(parse_blocks("net { } x;", errors), 2u);
  EXPECT_TRUE(errors.empty());

  microfmt::error_list<8> nested;
  EXPECT_EQ(parse_blocks("a { b { c d; e; } f; } g;", nested), 2u);
  EXPECT_EQ(nested.total(), 1u);
}

TEST(BlockRecovery, MissingCloserIsReportedAtEndOfInput) {
  microfmt::error_list<8> errors;
  EXPECT_EQ(parse_blocks("net { a; ", errors), 0u);
  ASSERT_GE(errors.total(), 1u);
  const auto stored = errors.errors();
  EXPECT_EQ(stored[0].message, "unexpected end of input");
}

TEST(ErrorNotes, SurviveErrorListAndPointAtTheOpener) {
  token_stream ts{"net { a;", lexer_rules{}};
  ts.next(); // net
  auto open = ts.expect(token_kind::punctuation, "{", "expected '{'");
  ASSERT_TRUE(open);
  while (!ts.at_end()) {
    ts.next();
  }
  microfmt::error_list<2> errors;
  errors.add(microfmt::with_note(ts.error_here("expected '}'"), "block opened here", open.value()));
  const auto stored = errors.errors();
  ASSERT_EQ(stored.size(), 1u);
  EXPECT_EQ(stored[0].note, "block opened here");
  EXPECT_EQ(stored[0].note_at.column, 5u);
}

namespace {

// Hands out `chunk` bytes per call, so tokens straddle every window refill.
struct chunk_reader {
  string_view data;
  size_t chunk;
  size_t pos{0};
  size_t operator()(reloco::span<char> out) noexcept {
    size_t n = data.size() - pos;
    n = n < chunk ? n : chunk;
    n = n < out.size() ? n : out.size();
    for (size_t i = 0; i < n; ++i) {
      out[i] = data[pos + i];
    }
    pos += n;
    return n;
  }
};

constexpr string_view kStreamCorpus =
    "name = \"a\\nb\"; // note\n  x == 1.5; /* block */ y = u8\"tail\" ; "
    "long_identifier_name_here = 0x2A; z != 7;\n";

lexer_rules stream_rules() {
  lexer_rules r = microfmt::rules::cpp;
  return r;
}

} // namespace

TEST(Streaming, MatchesContiguousLexingForEveryChunkSize) {
  const lexer_rules rules = stream_rules();
  token_stream whole{kStreamCorpus, rules};
  reloco::sso_vector<microfmt::token, 64> expected;
  for (microfmt::token t = whole.next(); t.kind != token_kind::end; t = whole.next()) {
    ASSERT_TRUE(expected.try_push_back(t));
  }

  for (const size_t chunk : {size_t{1}, size_t{2}, size_t{7}, size_t{64}, size_t{4096}}) {
    chunk_reader reader{kStreamCorpus, chunk};
    microfmt::stream_window<512> window{reader};
    token_stream ts{window.ref(), rules};
    size_t index = 0;
    for (microfmt::token t = ts.next(); t.kind != token_kind::end; t = ts.next(), ++index) {
      ASSERT_LT(index, expected.size()) << "chunk " << chunk;
      EXPECT_EQ(t.kind, expected[index].kind) << "chunk " << chunk;
      EXPECT_EQ(t.text, expected[index].text) << "chunk " << chunk;
      EXPECT_EQ(t.offset, expected[index].offset) << "chunk " << chunk;
      EXPECT_EQ(t.line, expected[index].line) << "chunk " << chunk;
      EXPECT_EQ(t.column, expected[index].column) << "chunk " << chunk;
    }
    EXPECT_EQ(index, expected.size()) << "chunk " << chunk;
    EXPECT_FALSE(ts.window_too_small());
  }
}

TEST(Streaming, OversizedTokenIsAnErrorAndTerminates) {
  std::string text = "\"";
  text.append(600, 'x');
  text += "\" tail";
  chunk_reader reader{text, 16};
  microfmt::stream_window<256> window{reader};
  token_stream ts{window.ref(), json_rules()};
  size_t guard = 0;
  bool saw_error = false;
  for (microfmt::token t = ts.next(); t.kind != token_kind::end && guard < 1000; t = ts.next(), ++guard) {
    saw_error = saw_error || t.kind == token_kind::error;
  }
  EXPECT_TRUE(saw_error);
  EXPECT_TRUE(ts.window_too_small());
  EXPECT_LT(guard, 1000u);
}

TEST(Streaming, DiscardLetsLargeInputPassWithAbsolutePositions) {
  std::string text;
  for (int i = 0; i < 400; ++i) {
    text += "k" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
  }
  chunk_reader reader{text, 5};
  microfmt::stream_window<256> window{reader};
  token_stream ts{window.ref(), stream_rules()};
  size_t statements = 0;
  microfmt::token last_name;
  while (!ts.at_end()) {
    last_name = ts.next();
    ASSERT_EQ(last_name.kind, token_kind::identifier);
    EXPECT_EQ(last_name.line, statements + 1);
    EXPECT_EQ(last_name.column, 1u);
    EXPECT_EQ(last_name.offset, text.find("k" + std::to_string(statements) + " ="));
    ASSERT_TRUE(ts.expect(token_kind::op, "=", "expected ="));
    ASSERT_TRUE(ts.expect(token_kind::integer, "expected number"));
    ASSERT_TRUE(ts.expect(token_kind::punctuation, ";", "expected ;"));
    ts.discard_consumed();
    ++statements;
  }
  EXPECT_EQ(statements, 400u);
  EXPECT_FALSE(ts.window_too_small());
  EXPECT_GT(window.ref().base(), 0u);
}

TEST(Streaming, StaleCopyReadsAsEnd) {
  std::string text(200, ' ');
  text += "a b c d e f g h";
  chunk_reader reader{text, 64};
  microfmt::stream_window<256> window{reader};
  token_stream ts{window.ref(), stream_rules()};
  token_stream before = ts;
  ts.next();
  ts.next();
  ts.discard_consumed();
  before.next(); // the two cached lookahead tokens are still returned
  before.next();
  EXPECT_EQ(before.next().kind, token_kind::end);
}

TEST(Streaming, BacktrackingWorksInsideTheWindow) {
  chunk_reader reader{"a b c", 1};
  microfmt::stream_window<256> window{reader};
  token_stream ts{window.ref(), stream_rules()};
  const token_stream saved = ts;
  EXPECT_EQ(ts.next().text, "a");
  EXPECT_EQ(ts.next().text, "b");
  ts = saved;
  EXPECT_EQ(ts.next().text, "a");
}

TEST(Streaming, TakeRawUntilCrossesChunkBordersAndCutsAtFullWindow) {
  std::string body(100, 'q');
  std::string text = "{" + body + "}";
  {
    chunk_reader reader{text, 3};
    microfmt::stream_window<256> window{reader};
    token_stream ts{window.ref(), stream_rules()};
    ASSERT_EQ(ts.next().text, "{");
    const microfmt::token raw = ts.take_raw_until("}");
    EXPECT_EQ(std::string(raw.text.data(), raw.text.size()), body);
    EXPECT_FALSE(ts.raw_cut());
    EXPECT_EQ(ts.next().text, "}");
  }
  {
    std::string big(600, 'q');
    chunk_reader reader{big, 50};
    microfmt::stream_window<256> window{reader};
    token_stream ts{window.ref(), stream_rules()};
    size_t total = 0;
    size_t pieces = 0;
    do {
      const microfmt::token raw = ts.take_raw_until("}");
      total += raw.text.size();
      ++pieces;
    } while (ts.raw_cut() && pieces < 20);
    EXPECT_EQ(total, 600u);
    EXPECT_GT(pieces, 1u);
  }
}

TEST(Streaming, ErrorListKeepsPositionsAfterTheSourceIsGone) {
  std::string text = "aa bb\n";
  text.append(120, ' ');
  text += "cc\n";
  chunk_reader reader{text, 8};
  microfmt::stream_window<256> window{reader};
  token_stream ts{window.ref(), stream_rules()};
  microfmt::error_list<4> errors;
  ts.next();
  errors.add(ts.error_here("first"));
  ts.next();
  ts.next();
  ts.discard_consumed();
  errors.add(ts.error_here("second"));
  ASSERT_EQ(errors.size(), 2u);
  const auto kept = errors.errors();
  EXPECT_EQ(kept[0].at.line, 1u);
  EXPECT_EQ(kept[0].at.column, 4u);
  EXPECT_EQ(kept[0].message, "first");
  EXPECT_EQ(kept[1].at.line, 3u);
}

TEST(Streaming, InternAndDecodeThroughThePool) {
  string_view text = "name \"a\\nb\" name \"x\\q\" next";
  chunk_reader reader{text, 3};
  microfmt::stream_window<256> window{reader};
  microfmt::string_pool<16> pool;
  const microfmt::string_pool_ref ref = pool.ref();
  token_stream ts{window.ref(), json_rules(), ref};

  auto first = microfmt::expect_name(ts, "name expected");
  ASSERT_TRUE(first);
  auto value = microfmt::expect_string(ts, "string expected", microfmt::decode_json_string);
  ASSERT_TRUE(value);
  EXPECT_EQ(pool.view(first.value()), "name");
  EXPECT_EQ(pool.view(value.value()), "a\nb");
  auto second = microfmt::expect_name(ts, "name expected");
  ASSERT_TRUE(second);
  EXPECT_EQ(first.value(), second.value());

  const size_t bytes_before = pool.bytes();
  auto bad = microfmt::expect_string(ts, "string expected", microfmt::decode_json_string);
  EXPECT_FALSE(bad);
  EXPECT_EQ(pool.bytes(), bytes_before);

  auto wrong = microfmt::expect_string(ts, "string expected", microfmt::decode_json_string);
  EXPECT_FALSE(wrong);
}

TEST(Streaming, DecodersCoverJsonCAndSql) {
  microfmt::string_pool<16> pool;
  const microfmt::string_pool_ref ref = pool.ref();
  auto json = ref.decode("\\u00e9\\ud83d\\ude00", microfmt::decode_json_string);
  ASSERT_TRUE(json.ok());
  EXPECT_EQ(pool.view(json.id), "\xC3\xA9\xF0\x9F\x98\x80");
  auto c = ref.decode("t\\tx\\x41", microfmt::decode_c_escapes);
  ASSERT_TRUE(c.ok());
  EXPECT_EQ(pool.view(c.id), "t\txA");
  auto sql = ref.decode("it''s", microfmt::decode_sql_string);
  ASSERT_TRUE(sql.ok());
  EXPECT_EQ(pool.view(sql.id), "it's");
}

TEST(Streaming, StoreAndViewDetachATokenFromTheSource) {
  microfmt::string_pool<16> pool;
  const microfmt::string_pool_ref ref = pool.ref();
  token_stream ts{"word", lexer_rules{}, ref};
  auto stored = microfmt::store(ts, ts.peek());
  ASSERT_TRUE(stored);
  const microfmt::token back = microfmt::view(ref, stored.value());
  EXPECT_EQ(back.text, "word");
  EXPECT_EQ(back.kind, token_kind::identifier);
  EXPECT_EQ(back.column, 1u);
}

TEST(Streaming, EndStatementClearsPerStatementStateButKeepsStrings) {
  chunk_reader reader{"a b", 2};
  microfmt::stream_window<256> window{reader};
  microfmt::string_pool<16> pool;
  const microfmt::string_pool_ref ref = pool.ref();
  token_stream ts{window.ref(), lexer_rules{}, ref};
  microfmt::memo_table<int, 4> memo;
  auto id = microfmt::expect_name(ts, "name");
  ASSERT_TRUE(id);
  microfmt::end_statement(ts, memo);
  EXPECT_EQ(pool.view(id.value()), "a");
  EXPECT_EQ(ts.peek().text, "b");
}

namespace {

struct paren_actions {
  using result = reloco::expected<int, microfmt::parse_error>;
  result primary(microfmt::token_stream &s) const {
    auto number = s.expect(microfmt::token_kind::integer, "expected a number");
    if (!number) {
      return reloco::unexpected<microfmt::parse_error>(number.error());
    }
    return static_cast<int>(number.value().text[0] - '0');
  }
  result make_binary(const microfmt::binary_op &, int l, int r) const { return l + r; }
  // Only used by the second test: a parenthesised value is tripled, and a trailing "!" is consumed here.
  result make_paren(const microfmt::token &, int inner, microfmt::token_stream &s) const {
    (void)s.skip(microfmt::token_kind::op, "!");
    return inner * 3;
  }
};

struct plain_actions {
  using result = reloco::expected<int, microfmt::parse_error>;
  result primary(microfmt::token_stream &s) const { return paren_actions{}.primary(s); }
  result make_binary(const microfmt::binary_op &, int l, int r) const { return l + r; }
};

} // namespace

TEST(ParseExpression, ParenHookSeesParenthesisedOperandsAndMaySkipSuffixes) {
  constexpr microfmt::binary_op kPlus[] = {{"+", 10}};
  constexpr microfmt::string_view kPlusBang[] = {"+", "!"};
  microfmt::lexer_rules rules;
  rules.operators = kPlusBang;
  const microfmt::expression_grammar grammar{kPlus, {}, {}, {}, {}};

  microfmt::token_stream hooked{"(1 + 2)! + 4", rules};
  paren_actions with_hook;
  auto value = microfmt::parse_expression<int>(hooked, grammar, with_hook);
  ASSERT_TRUE(value);
  EXPECT_EQ(value.value(), 13); // (1 + 2) * 3 + 4, and the "!" was consumed by the hook

  microfmt::token_stream plain{"(1 + 2) + 4", rules};
  plain_actions without_hook;
  auto unchanged = microfmt::parse_expression<int>(plain, grammar, without_hook);
  ASSERT_TRUE(unchanged);
  EXPECT_EQ(unchanged.value(), 7); // no make_paren: the inner value is used as is
}
