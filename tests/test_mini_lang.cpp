// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <string>

#include "../examples/mini_lang.hpp"

namespace {

using microfmt::string_view;
using mini_lang::interpreter;

} // namespace

TEST(MiniLang, EvaluatesExpressionsVariablesCallsAndTernary) {
  interpreter interp{
      "let x = 2 + 3 * 4; let y = x > 10 ? x : 0; print(max(x, y, 7)); print(min(4, 2)); print(abs(-9));\n"
      "let z; print(z); print(-(1 + 2) * 3); print(7 / 2 < 4 ? 1 : 2);"};
  EXPECT_EQ(interp.run(), 9u);
  EXPECT_TRUE(interp.errors().empty());
  EXPECT_EQ(interp.output(), "14\n2\n9\n0\n-9\n1\n");
}

TEST(MiniLang, BlocksRunAndShareVariables) {
  interpreter interp{"let a = 1; { let b = a + 1; { print(b); } } print(a);"};
  EXPECT_EQ(interp.run(), 3u);
  EXPECT_EQ(interp.output(), "2\n1\n");
}

TEST(MiniLang, EachBrokenStatementIsReportedOnceAndLaterOnesStillRun) {
  interpreter interp{"let a = 1;\nlet b = 1 +;\nprint(a;\nprint(a + 1);\n"};
  EXPECT_EQ(interp.run(), 2u);
  EXPECT_EQ(interp.output(), "2\n");
  const auto errors = interp.errors().errors();
  ASSERT_EQ(errors.size(), 2u);
  EXPECT_EQ(errors[0].message, "expected a number, variable or call");
  EXPECT_EQ(errors[0].at.line, 2u);
  EXPECT_EQ(errors[1].message, "expected ')'");
  EXPECT_EQ(errors[1].at.line, 3u);
}

TEST(MiniLang, ErrorInsideABlockStaysInsideAndSiblingsParse) {
  interpreter interp{"{ let c = nope; print(1); }\nprint(2);"};
  EXPECT_EQ(interp.run(), 2u); // the block rule itself succeeds, plus the final print
  EXPECT_EQ(interp.output(), "1\n2\n");
  ASSERT_EQ(interp.errors().total(), 1u);
  const auto errors = interp.errors().errors();
  EXPECT_EQ(errors[0].message, "unknown variable");
  EXPECT_EQ(errors[0].at.line, 1u);
}

TEST(MiniLang, UnclosedBlockHasANotePointingAtTheOpener) {
  interpreter interp{"print(1);\n{ let d = 1;\n"};
  (void)interp.run();
  const auto errors = interp.errors().errors();
  ASSERT_EQ(errors.size(), 2u);
  EXPECT_EQ(errors[0].message, "unexpected end of input");
  EXPECT_EQ(errors[1].message, "expected '}'");
  EXPECT_EQ(errors[1].note, "block opened here");
  EXPECT_EQ(errors[1].note_at.line, 2u);
  EXPECT_EQ(errors[1].note_at.column, 1u);
}

TEST(MiniLang, SemanticErrorsAreReportedAtTheRightToken) {
  const auto first_error = [](string_view program) {
    interpreter interp{program};
    (void)interp.run();
    const auto errors = interp.errors().errors();
    return errors.empty() ? string_view{} : errors[0].message;
  };
  EXPECT_EQ(first_error("print(1 / 0);"), "division by zero");
  EXPECT_EQ(first_error("print(foo(1));"), "unknown function");
  EXPECT_EQ(first_error("print(abs(1, 2));"), "wrong number of arguments");
  EXPECT_EQ(first_error("print(max(1,2,3,4,5,6,7,8,9));"), "too many arguments");
  EXPECT_EQ(first_error("print(99999999999999999999);"), "number out of range");
  EXPECT_EQ(first_error("7;"), "expected a statement: let, print or a block");
  EXPECT_EQ(first_error("let ;"), "expected a variable name");
  EXPECT_EQ(first_error("let x 5;"), "expected ';'");
  EXPECT_EQ(first_error("print(1);"), string_view{});
}

TEST(MiniLang, ResourceLimitsReportErrorsInsteadOfOverflowing) {
  string_view too_many =
      "let a=1;let b=1;let c=1;let d=1;let e=1;let f=1;let g=1;let h=1;let i=1;let j=1;let k=1;let l=1;let m=1;"
      "let n=1;let o=1;let p=1;let q=1;";
  interpreter vars{too_many};
  EXPECT_EQ(vars.run(), 16u);
  const auto var_errors = vars.errors().errors();
  ASSERT_EQ(var_errors.size(), 1u);
  EXPECT_EQ(var_errors[0].message, "too many variables");

  // 40 nested blocks exceed the nesting limit (32) without recursing further.
  string_view deep = "{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{{ print(1); }}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}";
  interpreter nested{deep};
  (void)nested.run();
  EXPECT_FALSE(nested.errors().empty());

  // Arithmetic wraps rather than overflowing (signed overflow would be undefined behaviour).
  interpreter wrap{"print(9223372036854775807 + 1); print(-9223372036854775807 - 1 / -1);"};
  (void)wrap.run();
  EXPECT_EQ(wrap.output().substr(0, 21), "-9223372036854775808\n");
}

TEST(MiniLang, IfThenElseIsAnExpressionThatNestsAndReportsMissingKeywords) {
  interpreter ok{
      "let a = 3; print(if a > 2 then a * 10 else 0); print(1 + if a < 2 then 1 else if a > 2 then 5 else 6);"};
  EXPECT_EQ(ok.run(), 3u);
  EXPECT_EQ(ok.output(), "30\n6\n");

  interpreter bad{"print(if 1 then 2);\nprint(if 1 2 else 3);\nprint(7);"};
  EXPECT_EQ(bad.run(), 1u);
  EXPECT_EQ(bad.output(), "7\n");
  const auto errors = bad.errors().errors();
  ASSERT_EQ(errors.size(), 2u);
  EXPECT_EQ(errors[0].message, "expected 'else'");
  EXPECT_EQ(errors[1].message, "expected 'then'");
}

TEST(MiniLang, EveryBadCallArgumentIsReportedAlongsideASummary) {
  interpreter interp{"print(max(1, , nope, 4));\nprint(2);"};
  EXPECT_EQ(interp.run(), 1u);
  EXPECT_EQ(interp.output(), "2\n");
  const auto errors = interp.errors().errors();
  ASSERT_EQ(errors.size(), 3u);
  EXPECT_EQ(errors[0].message, "expected a number, variable or call"); // the empty argument
  EXPECT_EQ(errors[1].message, "unknown variable");
  EXPECT_EQ(errors[1].at.length, 4u); // "nope"
  EXPECT_EQ(errors[2].message, "call has invalid arguments");
  EXPECT_EQ(errors[2].at.length, 3u); // "max"
}

TEST(MiniLang, OnlyTheChosenBranchRunsAndDivisionByZeroPointsAtTheDivisor) {
  interpreter lazy{"print(1 ? 5 : 1 / 0); print(if 0 then 1 / 0 else 6);"};
  EXPECT_EQ(lazy.run(), 2u);
  EXPECT_TRUE(lazy.errors().empty());
  EXPECT_EQ(lazy.output(), "5\n6\n");

  interpreter bad{"print(8 / (3 - 3));"};
  (void)bad.run();
  const auto errors = bad.errors().errors();
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors[0].message, "division by zero");
  EXPECT_EQ(errors[0].at.line, 1u);
}

TEST(MiniLang, ATooLargeExpressionFailsAndTheNodesAreReusedByLaterStatements) {
  std::string program = "print(1";
  for (int i = 0; i < 100; ++i) {
    program += "+1";
  }
  program += ");\nprint(1+1);";
  interpreter interp{program};
  EXPECT_EQ(interp.run(), 1u); // the second statement runs: the first one's nodes were given back
  EXPECT_EQ(interp.output(), "2\n");
  const auto errors = interp.errors().errors();
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors[0].message, "node pool full");
}

TEST(OperatorTable, LevelsOrderPrecedencesAndRenumberOnInsertion) {
  microfmt::operator_table<8> table;
  const auto loose = table.add_level();
  const auto tight = table.add_level();
  ASSERT_TRUE(loose && tight);
  EXPECT_LT(table.precedence_of(loose.value()), table.precedence_of(tight.value()));
  const auto middle = table.add_level_above(loose.value());
  ASSERT_TRUE(middle);
  EXPECT_LT(table.precedence_of(loose.value()), table.precedence_of(middle.value()));
  EXPECT_LT(table.precedence_of(middle.value()), table.precedence_of(tight.value()));
  EXPECT_TRUE(table.add_binary("+", middle.value()));
  EXPECT_EQ(table.level_of("+"), middle.value());
  EXPECT_FALSE(table.level_of("nope").valid());
}

TEST(OperatorTable, AtMost62LevelsAndStaticErrorsLeaveTheTableUnchanged) {
  microfmt::operator_table<4> table;
  for (size_t i = 0; i < microfmt::operator_table<4>::kMaxLevels; ++i) {
    ASSERT_TRUE(table.add_level()) << i;
  }
  const auto overflow = table.add_level();
  ASSERT_FALSE(overflow);
  EXPECT_EQ(overflow.error(), "operator table full");

  microfmt::operator_table<4> ops;
  ASSERT_TRUE(ops.add_binary("+", uint8_t{10}));
  const auto generation = ops.generation();
  EXPECT_EQ(ops.add_binary("+", uint8_t{20}).error(), "duplicate operator");
  EXPECT_EQ(ops.add_binary("", uint8_t{20}).error(), "empty operator");
  EXPECT_EQ(ops.generation(), generation);
  EXPECT_EQ(ops.size(), 1u);
  EXPECT_TRUE(ops.add_prefix("+", uint8_t{30})); // same spelling in another category is fine
}

TEST(OperatorTable, SpellingsAndKeywordsFeedTheLexer) {
  microfmt::operator_table<8> table;
  ASSERT_TRUE(table.add_binary("+", uint8_t{10}));
  ASSERT_TRUE(table.add_binary("<=", uint8_t{5}));
  ASSERT_TRUE(table.add_binary("div", uint8_t{20}));
  ASSERT_TRUE(table.add_prefix("(", uint8_t{30})); // punctuation is never an operator spelling
  EXPECT_EQ(table.spellings().size(), 2u);
  EXPECT_TRUE(table.keywords().contains("div"));
}

TEST(MiniLang, FixityDeclarationsAddOperatorsAtRunTime) {
  interpreter interp{"infixl 5 \"<>\" = max; print(1 + 2 <> 2 * 5); infixr 1 \"~\" = min; print(9 ~ 4 + 1);"};
  EXPECT_EQ(interp.run(), 4u);
  EXPECT_TRUE(interp.errors().empty());
  EXPECT_EQ(interp.output(), "10\n5\n");
}

TEST(MiniLang, NonAssociativeOperatorRejectsChainingAndBadDeclarationsAreReported) {
  interpreter chained{"infix 9 \"<>\" = max; print(1 <> 2);\nprint(1 <> 2 <> 3);"};
  EXPECT_EQ(chained.run(), 2u);
  const auto chained_errors = chained.errors().errors();
  ASSERT_EQ(chained_errors.size(), 1u);
  EXPECT_EQ(chained_errors[0].message, "operator is not associative");

  interpreter bad{"infixl 300 \"<>\" = max; infixl 5 \"+\" = max; infixl 5 \"<>\" = sum;"};
  EXPECT_EQ(bad.run(), 0u);
  EXPECT_EQ(bad.errors().errors().size(), 3u);
}
