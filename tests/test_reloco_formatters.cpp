// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/formatters/reloco.hpp>
#include <microfmt/microfmt.hpp>
#include <reloco/flat_map.hpp>
#include <reloco/flat_set.hpp>
#include <reloco/inline_flat_map.hpp>
#include <reloco/inline_flat_set.hpp>
#include <reloco/inline_string.hpp>
#include <reloco/inline_vector.hpp>
#include <reloco/shared_ptr.hpp>
#include <reloco/sso_string.hpp>
#include <reloco/string.hpp>
#include <reloco/unique_ptr.hpp>
#include <reloco/vector.hpp>

TEST(RelocoFormattersTest, FormatsVector) {
  reloco::vector<int> vec;
  ASSERT_TRUE(vec.try_reserve(3));
  ASSERT_TRUE(vec.try_push_back(1));
  ASSERT_TRUE(vec.try_push_back(2));
  ASSERT_TRUE(vec.try_push_back(3));

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", vec);
  EXPECT_EQ(buffer.view(), "[1, 2, 3]");
}

TEST(RelocoFormattersTest, FormatsEmptyVector) {
  reloco::vector<int> vec;

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", vec);
  EXPECT_EQ(buffer.view(), "[]");
}

TEST(RelocoFormattersTest, FormatsFlatSet) {
  reloco::flat_set<int> set;
  ASSERT_TRUE(set.try_insert(3).has_value());
  ASSERT_TRUE(set.try_insert(1).has_value());

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", set);
  EXPECT_EQ(buffer.view(), "[1, 3]");
}

TEST(RelocoFormattersTest, FormatsFlatMap) {
  reloco::flat_map<int, int> map;
  ASSERT_TRUE(map.try_insert(2, 20).has_value());
  ASSERT_TRUE(map.try_insert(1, 10).has_value());

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", map);
  EXPECT_EQ(buffer.view(), "{1: 10, 2: 20}");
}

TEST(RelocoFormattersTest, FormatsInlineVector) {
  reloco::inline_vector<int, 4> vec;
  ASSERT_TRUE(vec.try_push_back(1));
  ASSERT_TRUE(vec.try_push_back(2));

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", vec);
  EXPECT_EQ(buffer.view(), "[1, 2]");
}

TEST(RelocoFormattersTest, FormatsInlineFlatSet) {
  reloco::inline_flat_set<int, 4> set;
  ASSERT_TRUE(set.try_insert(5).has_value());
  ASSERT_TRUE(set.try_insert(2).has_value());

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", set);
  EXPECT_EQ(buffer.view(), "[2, 5]");
}

TEST(RelocoFormattersTest, FormatsInlineFlatMap) {
  reloco::inline_flat_map<int, int, 4> map;
  ASSERT_TRUE(map.try_insert(3, 30).has_value());

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", map);
  EXPECT_EQ(buffer.view(), "{3: 30}");
}

TEST(RelocoFormattersTest, FormatsOwningString) {
  const auto created = reloco::string::try_create(reloco::string_view("hello"));
  ASSERT_TRUE(created.has_value());

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", created.value());
  EXPECT_EQ(buffer.view(), "hello");
}

TEST(RelocoFormattersTest, FormatsInlineString) {
  const auto created = reloco::inline_string<16>::try_create(reloco::string_view("world"));
  ASSERT_TRUE(created.has_value());

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", created.value());
  EXPECT_EQ(buffer.view(), "world");
}

TEST(RelocoFormattersTest, FormatsSsoString) {
  const auto created = reloco::sso_string::try_create(reloco::string_view("sso"));
  ASSERT_TRUE(created.has_value());

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", created.value());
  EXPECT_EQ(buffer.view(), "sso");
}

TEST(RelocoFormattersTest, FormatsValuePtrAndNull) {
  int target = 5;
  const microfmt::value_ptr<int> ptr(&target);
  const microfmt::value_ptr<int> null_ptr(nullptr);

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", ptr);
  EXPECT_EQ(buffer.view(), "5");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{}", null_ptr);
  EXPECT_EQ(buffer.view(), "(null)");
}

TEST(RelocoFormattersTest, FormatsValueRef) {
  int target = 7;
  const microfmt::value_ref<int> ref(target);

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", ref);
  EXPECT_EQ(buffer.view(), "7");
}

TEST(RelocoFormattersTest, FormatsUniquePtrAndNull) {
  auto created = reloco::unique_ptr<int>::try_create(99);
  ASSERT_TRUE(created.has_value());
  reloco::unique_ptr<int> owned = std::move(created.value());
  const reloco::unique_ptr<int> empty;

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", owned);
  EXPECT_EQ(buffer.view(), "99");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{}", empty);
  EXPECT_EQ(buffer.view(), "(null)");
}

TEST(RelocoFormattersTest, FormatsSharedAndWeakPtr) {
  auto created = reloco::try_create_combined_shared<int>(8);
  ASSERT_TRUE(created.has_value());
  reloco::shared_ptr<int> shared = std::move(created.value());
  const reloco::weak_ptr<int> weak(shared);

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", shared);
  EXPECT_EQ(buffer.view(), "8");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{}", weak);
  EXPECT_EQ(buffer.view(), "8");

  shared.reset();
  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{}", weak);
  EXPECT_EQ(buffer.view(), "(expired)");
}

TEST(RelocoFormattersTest, FormatsCollectionView) {
  reloco::vector<int> vec;
  ASSERT_TRUE(vec.try_push_back(10));
  ASSERT_TRUE(vec.try_push_back(20));

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", microfmt::as_collection_view(vec));
  EXPECT_EQ(buffer.view(), "[10, 20]");
}
