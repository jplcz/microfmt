// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/parsing/lexer_rules_presets.hpp>
#include <microfmt/parsing/number_parse.hpp>
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
  EXPECT_EQ(errors[0].at.text, "x");
  EXPECT_EQ(errors[1].at.text, "y");

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
