// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/formatters/reloco_debug.hpp>
#include <microfmt/microfmt.hpp>
#include <reloco/any.hpp>

namespace {

struct unnamed_debug_test_type {
  int value;
};

} // namespace

TEST(RelocoDebugTest, AnyEmptyPrintsNoType) {
  reloco::any value;

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(value));
  EXPECT_EQ(buffer.view(), "any(<no type>)");
}

TEST(RelocoDebugTest, AnyWithRegisteredTypeNamePrintsIt) {
  // int has a RELOCO_TYPE_ID_NAME("int") registration from type_id.hpp.
  reloco::result<reloco::any> created = reloco::any::try_create(42);
  ASSERT_TRUE(created.has_value());
  reloco::any value = std::move(created).value();

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(value));
  EXPECT_EQ(buffer.view(), "any(int)");
}

TEST(RelocoDebugTest, AnyWithUnregisteredTypeNamePrintsUnnamed) {
  reloco::result<reloco::any> created = reloco::any::try_create(unnamed_debug_test_type{7});
  ASSERT_TRUE(created.has_value());
  reloco::any value = std::move(created).value();

  microfmt::buffer_sink<64> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(value));
  EXPECT_EQ(buffer.view(), "any(<unnamed type>)");
}
