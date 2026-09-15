// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/formatters/source_location.hpp>
#include <microfmt/microfmt.hpp>

#if MICROFMT_HAS_STD_SOURCE_LOCATION
TEST(SourceLocationTest, FormatsFileLineAndOptionalFunction) {
  const auto full = microfmt::format<512>("{}", microfmt::source_loc());
  EXPECT_NE(full.view().find("test_source_location.cpp"), std::string_view::npos);
  EXPECT_NE(full.view().find(':'), std::string_view::npos);
  EXPECT_NE(full.view().find(" in "), std::string_view::npos);

  const auto short_form =
      microfmt::format<256>("{:s}", microfmt::source_loc());
  EXPECT_NE(short_form.view().find("test_source_location.cpp"),
            std::string_view::npos);
  EXPECT_EQ(short_form.view().find(" in "), std::string_view::npos);
}

TEST(SourceLocationTest, FormatsSourceLocationDirectly) {
  const auto rendered =
      microfmt::format<512>("{}", std::source_location::current());

  EXPECT_NE(rendered.view().find("test_source_location.cpp"),
            std::string_view::npos);
  EXPECT_NE(rendered.view().find(':'), std::string_view::npos);
}
#endif
