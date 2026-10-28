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
constexpr binary_op kBinary[] = {{"+", 10, assoc::left}, {"-", 10, assoc::left}, {"*", 20, assoc::left},
                                 {"/", 20, assoc::left}, {"^", 40, assoc::right}};
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
  EXPECT_EQ(calc_post("5[2]", 50).value(), 52);  // callback consumes the index and ']'
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
  EXPECT_EQ(calc_ternary("0 ? 1 : 0 ? 2 : 3").value(), 3);    // a ? b : (c ? d : e)
  EXPECT_EQ(calc_ternary("1 ? 0 ? 1 : 2 : 3").value(), 2);    // nested in the middle operand
  EXPECT_EQ(calc_ternary("1 + 0 ? 5 : 6").value(), 5);        // binary binds tighter: (1 + 0) ? 5 : 6
  EXPECT_EQ(calc_ternary("0 ? 5 : 1 + 1").value(), 2);        // else part is a full binary expression
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
