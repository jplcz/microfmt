#include <gtest/gtest.h>
#include <microfmt/parsing/number_parse.hpp>

namespace {

using microfmt::integer_options;
using microfmt::number_errc;
using microfmt::parse;
using microfmt::parse_prefix;

template <typename T> void expect_value(microfmt::string_view text, T expected, const integer_options &o = {}) {
  auto r = parse<T>(text, o);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r.value(), expected);
}

template <typename T>
void expect_error(microfmt::string_view text, number_errc code, std::size_t offset, const integer_options &o = {}) {
  auto r = parse<T>(text, o);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, code);
  EXPECT_EQ(r.error().offset, offset);
}

TEST(NumberParse, SignedLimits) {
  expect_value<std::int8_t>("127", 127);
  expect_value<std::int8_t>("-128", -128);
  expect_error<std::int8_t>("128", number_errc::overflow, 2);
  expect_error<std::int8_t>("-129", number_errc::underflow, 3);
  expect_value<std::int64_t>("-9223372036854775808", INT64_MIN);
  expect_error<std::int64_t>("9223372036854775808", number_errc::overflow, 18);
  expect_value<std::int32_t>("-0", 0);
  expect_value<std::int32_t>("+42", 42);
}

TEST(NumberParse, UnsignedLimits) {
  expect_value<std::uint8_t>("255", 255);
  expect_error<std::uint8_t>("256", number_errc::overflow, 2);
  expect_error<std::uint8_t>("-1", number_errc::invalid_digit, 0);
  expect_value<std::uint64_t>("18446744073709551615", UINT64_MAX);
  expect_error<std::uint64_t>("18446744073709551616", number_errc::overflow, 19);
}

TEST(NumberParse, EmptyAndInvalid) {
  expect_error<int>("", number_errc::empty, 0);
  expect_error<int>("-", number_errc::empty, 1);
  expect_error<int>("+", number_errc::empty, 1);
  expect_error<int>("a", number_errc::invalid_digit, 0);
  expect_error<int>("12x", number_errc::invalid_digit, 2);
  expect_error<int>("+1", number_errc::invalid_digit, 0, integer_options{.allow_plus = false});
  expect_error<int>("1", number_errc::invalid_radix, 0, integer_options{.radix = 40});
}

TEST(NumberParse, Radix) {
  expect_value<int>("ff", 255, integer_options{.radix = 16});
  expect_value<int>("FF", 255, integer_options{.radix = 16});
  expect_value<int>("101", 5, integer_options{.radix = 2});
  expect_value<int>("zz", 35 * 36 + 35, integer_options{.radix = 36});
  expect_error<int>("12", number_errc::invalid_digit, 1, integer_options{.radix = 2});
}

TEST(NumberParse, Prefixes) {
  const integer_options detect{.radix = 0};
  expect_value<int>("0xFF", 255, detect);
  expect_value<int>("0b101", 5, detect);
  expect_value<int>("0o17", 15, detect);
  expect_value<int>("017", 17, detect);
  expect_value<int>("-0x10", -16, detect);
  expect_error<int>("0x", number_errc::empty, 2, detect);
  expect_value<int>("0x1f", 31, integer_options{.radix = 16, .allow_prefix = true});
  expect_value<int>("1f", 31, integer_options{.radix = 16, .allow_prefix = true});
  // without allow_prefix, 'b' is just a hex digit
  expect_value<int>("0b1", 0xb1, integer_options{.radix = 16});
}

TEST(NumberParse, Separators) {
  const integer_options sep{.separator = '_'};
  expect_value<int>("1_000_000", 1000000, sep);
  expect_error<int>("_1", number_errc::invalid_digit, 0, sep);
  expect_error<int>("1__0", number_errc::invalid_digit, 1, sep);
  expect_error<int>("1_", number_errc::invalid_digit, 1, sep);
  expect_value<unsigned>("0xdead_beef", 0xdeadbeefU, integer_options{.radix = 0, .separator = '_'});
}

TEST(NumberParse, PrefixParse) {
  auto r = parse_prefix<int>("123abc");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r.value().value, 123);
  EXPECT_EQ(r.value().consumed, 3U);
  auto s = parse_prefix<int>("1_2_", integer_options{.separator = '_'});
  ASSERT_TRUE(s.has_value());
  EXPECT_EQ(s.value().value, 12);
  EXPECT_EQ(s.value().consumed, 3U);
  EXPECT_FALSE(parse_prefix<int>("x1").has_value());
}

#ifdef __SIZEOF_INT128__
__extension__ typedef __int128 int128_type;
__extension__ typedef unsigned __int128 uint128_type;

TEST(NumberParse, Int128) {
  auto r = parse<uint128_type>("340282366920938463463374607431768211455");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r.value(), ~static_cast<uint128_type>(0));
  auto o = parse<uint128_type>("340282366920938463463374607431768211456");
  ASSERT_FALSE(o.has_value());
  EXPECT_EQ(o.error().code, number_errc::overflow);
  auto m = parse<int128_type>("-170141183460469231731687303715884105728");
  ASSERT_TRUE(m.has_value());
  EXPECT_TRUE(m.value() < 0);
}
#endif

TEST(NumberParse, WideFixedInt) {
  using u256 = reloco::fixed_int<256, false>;
  // 2^255 = 57896044618658097711785492504343953926634992332820282019728792003956564819968
  auto r = parse<u256>("57896044618658097711785492504343953926634992332820282019728792003956564819968");
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(r.value() == (static_cast<u256>(1) << 255));
  // 2^256 overflows
  auto o = parse<u256>("115792089237316195423570985008687907853269984665640564039457584007913129639936");
  ASSERT_FALSE(o.has_value());
  EXPECT_EQ(o.error().code, number_errc::overflow);
  using i256 = reloco::fixed_int<256, true>;
  auto n = parse<i256>("-57896044618658097711785492504343953926634992332820282019728792003956564819968");
  ASSERT_TRUE(n.has_value());
  EXPECT_TRUE(n.value() < static_cast<i256>(0));
}

} // namespace
