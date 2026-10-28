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
