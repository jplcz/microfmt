// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <boost/asio/ip/tcp.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/describe.hpp>
#include <boost/dynamic_bitset.hpp>
#include <boost/logic/tribool.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/optional.hpp>
#include <boost/outcome/result.hpp>
#include <boost/rational.hpp>
#include <boost/system/error_code.hpp>
#include <boost/variant2/variant.hpp>

#include <microfmt/formatters/boost_containers.hpp>
#include <microfmt/formatters/boost_describe.hpp>
#include <microfmt/formatters/boost_monad.hpp>
#include <microfmt/formatters/boost_net.hpp>
#include <microfmt/formatters/boost_system.hpp>
#include <microfmt/formatters/boost_time.hpp>
#include <microfmt/formatters/boost_values.hpp>
#include <microfmt/microfmt.hpp>

enum class described_state : uint8_t { idle = 0, running = 1 };
BOOST_DESCRIBE_ENUM(described_state, idle, running)

struct described_record {
  int id;
  bool ready;
};
BOOST_DESCRIBE_STRUCT(described_record, (), (id, ready))

TEST(BoostFormattersTest, FormatsDescribedEnumsAndStructs) {
  EXPECT_EQ(microfmt::format<32>("{} {}", described_state::running,
                                static_cast<described_state>(9))
                .view(),
            "running static_cast<uint8_t>(9)");
  EXPECT_EQ(
      microfmt::format<64>("{}", described_record{7, true}).view(),
      "{id: 7, ready: true}");
}

TEST(BoostFormattersTest, FormatsOptionalVariantAndOutcome) {
  microfmt::buffer_sink<128> output;

  microfmt::format_to(output.as_sink(), "{:04x}", boost::optional<int>{42});
  EXPECT_EQ(output.view(), "Some(002a)");

  output.reset();
  microfmt::format_to(output.as_sink(), "{:#04x}",
                      boost::variant2::variant<int, bool>{42});
  EXPECT_EQ(output.view(), "variant[0](002a)");

  output.reset();
  using test_result =
      boost::outcome_v2::basic_result<int, short,
                                      boost::outcome_v2::policy::all_narrow>;
  test_result success{boost::outcome_v2::success(42)};
  microfmt::format_to(output.as_sink(), "{:04x}", success);
  EXPECT_EQ(output.view(), "Ok(002a)");

  output.reset();
  test_result failure{
      boost::outcome_v2::failure(short{7})};
  microfmt::format_to(output.as_sink(), "{}", failure);
  EXPECT_EQ(output.view(), "Err(7)");
}

TEST(BoostFormattersTest, FormatsSystemErrorsAndContainers) {
  microfmt::buffer_sink<128> output;

  const auto error =
      boost::system::errc::make_error_code(boost::system::errc::invalid_argument);
  microfmt::format_to(output.as_sink(), "{}", error);
  EXPECT_EQ(output.view(), "generic:22");

  output.reset();
  const boost::container::static_vector<int, 4> fixed{1, 2, 3};
  microfmt::format_to(output.as_sink(), "{:02x}", fixed);
  EXPECT_EQ(output.view(), "[01, 02, 03]");

  output.reset();
  const boost::container::small_vector<int, 2> small{4, 5, 6};
  microfmt::format_to(output.as_sink(), "{}", small);
  EXPECT_EQ(output.view(), "[4, 5, 6]");
}

TEST(BoostFormattersTest, FormatsAddressesAndEndpointsWithoutStringConversion) {
  microfmt::buffer_sink<128> output;

  const boost::asio::ip::address_v4 v4({192, 0, 2, 10});
  microfmt::format_to(output.as_sink(), "{}", v4);
  EXPECT_EQ(output.view(), "192.0.2.10");

  output.reset();
  boost::asio::ip::address_v6::bytes_type v6_bytes{};
  v6_bytes[15] = 1;
  const boost::asio::ip::address_v6 v6(v6_bytes);
  microfmt::format_to(output.as_sink(), "{}", v6);
  EXPECT_EQ(output.view(), "::1");

  output.reset();
  const boost::asio::ip::tcp::endpoint endpoint(v6, 443);
  microfmt::format_to(output.as_sink(), "{}", endpoint);
  EXPECT_EQ(output.view(), "[::1]:443");
}

TEST(BoostFormattersTest, FormatsBitsetRationalTriboolAndCppInt) {
  microfmt::buffer_sink<256> output;

  const boost::dynamic_bitset<> bits(8, 0xad);
  microfmt::format_to(output.as_sink(), "{:#x}", bits);
  EXPECT_EQ(output.view(), "0xad");

  output.reset();
  microfmt::format_to(output.as_sink(), "{}", boost::rational<int>(2, 3));
  EXPECT_EQ(output.view(), "2/3");

  output.reset();
  const boost::logic::tribool unknown = boost::logic::indeterminate;
  microfmt::format_to(output.as_sink(), "{} {} {}", boost::logic::tribool(true),
                      boost::logic::tribool(false), unknown);
  EXPECT_EQ(output.view(), "true false indeterminate");

  output.reset();
  boost::multiprecision::cpp_int integer = 1;
  integer <<= 68;
  integer += 0x2a;
  microfmt::format_to(output.as_sink(), "{:#X}", integer);
  EXPECT_EQ(output.view(), "0X10000000000000002A");
}

TEST(BoostFormattersTest, FormatsBoostChronoAndDateTime) {
  microfmt::buffer_sink<128> output;

  microfmt::format_to(output.as_sink(), "{}",
                      boost::chrono::milliseconds(125));
  EXPECT_EQ(output.view(), "125ms");

  output.reset();
  const boost::gregorian::date date(2026, 9, 17);
  microfmt::format_to(output.as_sink(), "{}", date);
  EXPECT_EQ(output.view(), "2026-09-17");

  output.reset();
  const boost::posix_time::ptime time(
      date, boost::posix_time::hours(18) + boost::posix_time::minutes(28) +
                boost::posix_time::seconds(15));
  microfmt::format_to(output.as_sink(), "{}", time);
  EXPECT_EQ(output.view(), "2026-09-17T18:28:15");
}
