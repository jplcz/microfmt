// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/formatters/reloco_debug.hpp>
#include <microfmt/microfmt.hpp>
#include <reloco/any.hpp>
#include <reloco/flat_hash_map.hpp>
#include <reloco/flat_hash_set.hpp>
#include <reloco/rc.hpp>

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

TEST(RelocoDebugTest, ErrorPrintsMemberName) {
  const reloco::error value = reloco::error::out_of_range;

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(value));
  EXPECT_FALSE(buffer.view().empty());
}

TEST(RelocoDebugTest, ResultOkPrintsValue) {
  reloco::result<int> value = 42;

  microfmt::buffer_sink<16> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(value));
  EXPECT_FALSE(buffer.view().empty());
}

TEST(RelocoDebugTest, ResultErrPrintsErrorName) {
  reloco::result<int> value = reloco::unexpected(reloco::error::not_found);

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(value));
  EXPECT_FALSE(buffer.view().empty());
}

TEST(RelocoDebugTest, ResultVoidOkPrintsEmptyOk) {
  reloco::result<void> value;

  microfmt::buffer_sink<16> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(value));
  EXPECT_FALSE(buffer.view().empty());
}

TEST(RelocoDebugTest, ResultVoidErrPrintsErrorName) {
  reloco::result<void> value = reloco::unexpected(reloco::error::timed_out);

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(value));
  EXPECT_FALSE(buffer.view().empty());
}

TEST(RelocoDebugTest, RcPrintsUseCountAndValue) {
  auto created = reloco::try_create_combined_rc<int>(8);
  ASSERT_TRUE(created.has_value());
  reloco::rc<int> shared = std::move(created.value());
  reloco::rc<int> shared_copy = shared;

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(shared));
  EXPECT_FALSE(buffer.view().empty());
}

TEST(RelocoDebugTest, RcNullPrintsUseCountZero) {
  const reloco::rc<int> empty;

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(empty));
  EXPECT_FALSE(buffer.view().empty());
}

TEST(RelocoDebugTest, WeakRcPrintsUseCountAndValue) {
  auto created = reloco::try_create_combined_rc<int>(8);
  ASSERT_TRUE(created.has_value());
  reloco::rc<int> shared = std::move(created.value());
  const reloco::weak_rc<int> weak(shared);

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(weak));
  EXPECT_FALSE(buffer.view().empty());
}

TEST(RelocoDebugTest, WeakRcExpiredPrintsExpired) {
  reloco::weak_rc<int> weak;
  {
    auto created = reloco::try_create_combined_rc<int>(8);
    ASSERT_TRUE(created.has_value());
    reloco::rc<int> shared = std::move(created.value());
    weak = reloco::weak_rc<int>(shared);
  }

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(weak));
  EXPECT_FALSE(buffer.view().empty());
}

TEST(RelocoDebugTest, FlatHashSetPrintsDiagnosticsAndElements) {
  reloco::flat_hash_set<int> set;
  ASSERT_TRUE(set.try_insert(1).has_value());
  ASSERT_TRUE(set.try_insert(2).has_value());

  microfmt::buffer_sink<128> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(set));
  EXPECT_FALSE(buffer.view().empty());
}

TEST(RelocoDebugTest, FlatHashMapPrintsDiagnosticsAndEntries) {
  reloco::flat_hash_map<int, int> map;
  ASSERT_TRUE(map.try_insert(1, 10).has_value());

  microfmt::buffer_sink<128> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_debug(map));
  EXPECT_FALSE(buffer.view().empty());
}
