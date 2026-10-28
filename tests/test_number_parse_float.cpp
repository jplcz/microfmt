#include <gtest/gtest.h>
#include <microfmt/parsing/number_parse_float.hpp>

namespace {

using microfmt::float_options;
using microfmt::number_errc;
using microfmt::parse;
using microfmt::parse_prefix;

TEST(NumberParseFloat, Basics) {
  auto a = parse<double>("0.1");
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(a.value(), 0.1);
  EXPECT_EQ(parse<double>("-1.5e3").value(), -1500.0);
  EXPECT_EQ(parse<double>("+2E-2").value(), 0.02);
  EXPECT_EQ(parse<double>("7").value(), 7.0);
  EXPECT_EQ(parse<float>("0.1").value(), 0.1F);
  EXPECT_NE(static_cast<double>(parse<float>("0.1").value()), 0.1);
  EXPECT_EQ(parse<long double>("2.5").value(), 2.5L);
}

TEST(NumberParseFloat, Errors) {
  EXPECT_EQ(parse<double>("").error().code, number_errc::empty);
  EXPECT_EQ(parse<double>("-").error().code, number_errc::empty);
  EXPECT_EQ(parse<double>("abc").error().code, number_errc::invalid_digit);
  auto t = parse<double>("1.5x");
  ASSERT_FALSE(t.has_value());
  EXPECT_EQ(t.error().code, number_errc::invalid_digit);
  EXPECT_EQ(t.error().offset, 3U);
  EXPECT_EQ(parse<double>("1e400").error().code, number_errc::overflow);
  EXPECT_EQ(parse<double>("-1e400").error().code, number_errc::underflow);
  EXPECT_FALSE(parse<double>("+1", float_options{.allow_plus = false}).has_value());
}

TEST(NumberParseFloat, RangeBehaviour) {
  EXPECT_TRUE(std::isinf(parse<double>("1e400", float_options{.overflow_is_error = false}).value()));
  auto sub = parse<double>("1e-320");
  ASSERT_TRUE(sub.has_value());
  EXPECT_GT(sub.value(), 0.0);
  EXPECT_EQ(parse<double>("1e-400").value(), 0.0);
}

TEST(NumberParseFloat, Dots) {
  EXPECT_EQ(parse<double>(".5").value(), 0.5);
  EXPECT_EQ(parse<double>("5.").value(), 5.0);
  EXPECT_FALSE(parse<double>(".5", float_options{.allow_leading_dot = false}).has_value());
  EXPECT_FALSE(parse<double>("5.", float_options{.allow_trailing_dot = false}).has_value());
  EXPECT_FALSE(parse<double>(".").has_value());
}

TEST(NumberParseFloat, PrefixStopsAtGarbage) {
  auto r = parse_prefix<double>("1.5abc");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r.value().value, 1.5);
  EXPECT_EQ(r.value().consumed, 3U);
  auto e = parse_prefix<double>("1e");
  ASSERT_TRUE(e.has_value());
  EXPECT_EQ(e.value().consumed, 1U);
  auto d = parse_prefix<double>("5.e", float_options{});
  ASSERT_TRUE(d.has_value());
  EXPECT_EQ(d.value().consumed, 2U);
}

TEST(NumberParseFloat, SeparatorsSpecialsHex) {
  const float_options sep{.separator = '_'};
  EXPECT_EQ(parse<double>("1_000.5_5", sep).value(), 1000.55);
  EXPECT_FALSE(parse<double>("1__0", sep).has_value());
  EXPECT_FALSE(parse<double>("inf").has_value());
  const float_options sp{.allow_special = true};
  EXPECT_TRUE(std::isinf(parse<double>("-Infinity", sp).value()));
  EXPECT_TRUE(parse<double>("-inf", sp).value() < 0);
  EXPECT_TRUE(std::isnan(parse<double>("NaN", sp).value()));
  EXPECT_EQ(parse<double>("0x1.8p1", float_options{.allow_hex = true}).value(), 3.0);
  EXPECT_FALSE(parse<double>("0x1p1").has_value());
}

TEST(NumberParseFloat, TooLong) {
  std::string digits(2000, '1');
  auto r = parse<double>(microfmt::string_view(digits.data(), digits.size()));
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, number_errc::invalid_digit);
}

} // namespace
