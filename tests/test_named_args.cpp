// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <string>

#include <microfmt/microfmt.hpp>

namespace {

template <typename... Args> std::string render(microfmt::string_view fmt, const Args &...args) {
  microfmt::buffer_sink<256> buf;
  microfmt::format_to(buf.as_sink(), fmt, args...);
  return std::string(buf.view().data(), buf.view().size());
}

using microfmt::arg;

} // namespace

TEST(NamedArgsTest, ResolvesFieldsByName) {
  EXPECT_EQ(render("{user} from {ip}", arg("user", "ann"), arg("ip", 7)), "ann from 7");
}

TEST(NamedArgsTest, FieldsMayBeReorderedAndRepeated) {
  EXPECT_EQ(render("{b}{a}{b}", arg("a", 1), arg("b", 2)), "212");
}

TEST(NamedArgsTest, SpecAfterColonStillApplies) { EXPECT_EQ(render("[{n:5}] [{n:x}]", arg("n", 255)), "[  255] [ff]"); }

TEST(NamedArgsTest, UnknownNamePrintsMissing) { EXPECT_EQ(render("{nope}", arg("a", 1)), "{MISSING}"); }

TEST(NamedArgsTest, NameWithoutAnyNamedArgumentPrintsMissing) { EXPECT_EQ(render("{a}", 1), "{MISSING}"); }

TEST(NamedArgsTest, FirstOfDuplicateNamesWins) { EXPECT_EQ(render("{a}", arg("a", 1), arg("a", 2)), "1"); }

TEST(NamedArgsTest, MixesPositionalAndNamed) { EXPECT_EQ(render("{0} -> {target}", 5, arg("target", "x")), "5 -> x"); }

TEST(NamedArgsTest, AutoIndexCountsNamedArguments) { EXPECT_EQ(render("{} {}", arg("a", 1), 2), "1 2"); }

TEST(NamedArgsTest, NamedArgumentIsReachableByIndex) { EXPECT_EQ(render("{1}", 9, arg("a", 4)), "4"); }

TEST(NamedArgsTest, EscapedBracesStayLiteral) { EXPECT_EQ(render("{{a}} {a}", arg("a", 1)), "{a} 1"); }

TEST(NamedArgsTest, IdentifierMayContainDigitsAndUnderscores) { EXPECT_EQ(render("{_x1}", arg("_x1", 3)), "3"); }

TEST(NamedArgsTest, WorksThroughMakeFormatArgs) {
  const int twelve = 12;
  const int three = 3;
  const auto named = arg("v", twelve); // the store borrows it, so it must be a variable
  const auto store = microfmt::make_format_args(named, three);
  microfmt::buffer_sink<64> buf;
  microfmt::vformat_args_to(buf.as_sink(), "{v}/{1}", store);
  EXPECT_EQ(std::string(buf.view().data(), buf.view().size()), "12/3");
}

TEST(NamedArgsTest, WorksWithCompileTimeFormatString) {
  microfmt::buffer_sink<64> buf;
  microfmt::format_to(buf.as_sink(), MICROFMT_STRING("{n}!"), arg("n", 8));
  EXPECT_EQ(std::string(buf.view().data(), buf.view().size()), "8!");
}

TEST(DynamicFormatArgsTest, FormatsFieldsAddedAtRuntime) {
  const microfmt::string_view user = "ann";
  const int count = 3;
  microfmt::dynamic_format_args<4> args;
  EXPECT_TRUE(args.push("user", user));
  EXPECT_TRUE(args.push("count", count));

  microfmt::buffer_sink<64> buf;
  microfmt::vformat_args_to(buf.as_sink(), "{count} x {user}", args);
  EXPECT_EQ(std::string(buf.view().data(), buf.view().size()), "3 x ann");
}

TEST(DynamicFormatArgsTest, RejectsPushBeyondCapacity) {
  const int a = 1;
  const int b = 2;
  microfmt::dynamic_format_args<1> args;
  EXPECT_TRUE(args.push("a", a));
  EXPECT_FALSE(args.push("b", b));
  EXPECT_EQ(args.size(), 1U);

  microfmt::buffer_sink<64> buf;
  microfmt::vformat_args_to(buf.as_sink(), "{a}{b}", args);
  EXPECT_EQ(std::string(buf.view().data(), buf.view().size()), "1{MISSING}");
}

TEST(DynamicFormatArgsTest, UnnamedArgumentsAreReachableByPosition) {
  const int a = 5;
  microfmt::dynamic_format_args<2> args;
  EXPECT_TRUE(args.push(a));

  microfmt::buffer_sink<64> buf;
  microfmt::vformat_args_to(buf.as_sink(), "{} {0}", args);
  EXPECT_EQ(std::string(buf.view().data(), buf.view().size()), "5 5");
}
