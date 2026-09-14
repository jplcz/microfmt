// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <microfmt/formatters/styled.hpp>
#include <microfmt/microfmt.hpp>

namespace {

template <typename... Args>
std::string_view format_styled(microfmt::buffer_sink<128> &output,
                               std::string_view fmt,
                               const Args &...args) {
  output.reset();
  microfmt::format_to(output.as_sink(), fmt, args...);
  return output.view();
}

} // namespace

TEST(StyledTest, FactoryHelpersApplyPaddingAndCasing) {
  microfmt::buffer_sink<128> output;

  EXPECT_EQ(format_styled(output, "{}", microfmt::pad("io", 5)), "io   ");
  EXPECT_EQ(format_styled(output, "{}", microfmt::pad_center("io", 6, '.')),
            "..io..");
  EXPECT_EQ(format_styled(output, "{}", microfmt::pad_right("io", 5, '_')),
            "___io");
  EXPECT_EQ(format_styled(output, "{} {}", microfmt::to_upper("Cpu"),
                          microfmt::to_lower("VOLTAGE")),
            "CPU voltage");
}

TEST(StyledTest, QuotesAndTruncationAreAppliedBeforePadding) {
  microfmt::buffer_sink<128> output;

  EXPECT_EQ(format_styled(output, "{}", microfmt::quoted(
                                         "sensor", microfmt::quote_style::parens)),
            "(sensor)");
  EXPECT_EQ(format_styled(output, "{}",
                          microfmt::truncate("abcdefgh", 5)),
            "ab...");
  EXPECT_EQ(format_styled(output, "{}",
                          microfmt::truncate("abcdefgh", 3)),
            "abc");
  EXPECT_EQ(format_styled(output, "{:_^12}",
                          microfmt::quoted("io", microfmt::quote_style::brackets)),
            "____[io]____");
}

TEST(StyledTest, FormatSpecifierOverridesViewSettings) {
  microfmt::buffer_sink<128> output;

  EXPECT_EQ(format_styled(output, "{:*^10u}", microfmt::pad_right("cpu", 6)),
            "***CPU****");
  EXPECT_EQ(format_styled(output, "{:>12tq.5}", microfmt::to_lower(
                                                        "DEVICE_NAME")),
            "     \"De...\"");
  EXPECT_EQ(format_styled(output, "{:b}", microfmt::to_upper("ok")), "[OK]");
}
