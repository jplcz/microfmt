// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/value_ptr.hpp>
#include <microfmt/value_ref.hpp>
#include <microfmt/formatters/hash.hpp>
#include <microfmt/formatters/variant.hpp>

#include <type_traits>
#include <utility>
#include <variant>

namespace {

struct base_value {
  int value;
};

struct derived_value : base_value {};

struct unrelated_value {};

static_assert(std::is_constructible_v<microfmt::value_ref<int>, int &>);
static_assert(
    std::is_constructible_v<microfmt::value_ref<int>, const int &>);
static_assert(!std::is_constructible_v<microfmt::value_ref<int>, int &&>);
static_assert(
    !std::is_constructible_v<microfmt::value_ref<int>, const int &&>);
static_assert(
    std::is_constructible_v<microfmt::value_ref<base_value>, derived_value &>);
static_assert(!std::is_constructible_v<microfmt::value_ref<base_value>,
                                       unrelated_value &>);
static_assert(
    std::is_same_v<decltype(*std::declval<microfmt::value_ref<int> &>()),
                   const int &>);
static_assert(
    std::is_same_v<decltype(std::declval<microfmt::value_ref<int> &>().get()),
                   const int *>);
static_assert(std::is_trivially_copyable_v<microfmt::value_ptr<int>>);
static_assert(sizeof(microfmt::value_ptr<int>) == sizeof(int *));
static_assert(std::is_constructible_v<microfmt::value_ptr<int>, int *>);
static_assert(
    std::is_constructible_v<microfmt::value_ptr<const int>, int *>);
static_assert(
    !std::is_constructible_v<microfmt::value_ptr<int>, const int *>);
static_assert(
    std::is_constructible_v<microfmt::value_ptr<base_value>, derived_value *>);
static_assert(
    std::is_constructible_v<microfmt::value_ptr<void>, derived_value *>);
static_assert(std::is_constructible_v<microfmt::hash_view<int>, const int &>);
static_assert(!std::is_constructible_v<microfmt::hash_view<int>, int &&>);
static_assert(std::is_constructible_v<microfmt::variant_view<int>,
                                      const std::variant<int> &>);
static_assert(!std::is_constructible_v<microfmt::variant_view<int>,
                                       std::variant<int> &&>);

TEST(ValueRef, PreservesReferencedObjectIdentity) {
  int value = 42;
  microfmt::value_ref<int> ref(value);

  EXPECT_EQ(ref.get(), &value);
  EXPECT_EQ(*ref, 42);

  value = 7;
  EXPECT_EQ(*ref, 7);
}

TEST(ValueRef, ProvidesReadOnlyMemberAccess) {
  derived_value value{{11}};
  microfmt::value_ref<base_value> ref(value);

  EXPECT_EQ(ref.get(), static_cast<base_value *>(&value));
  EXPECT_EQ(ref->value, 11);
}

TEST(ValueRef, DeductionGuidePreservesReferencedType) {
  const int value = 19;
  microfmt::value_ref ref(value);

  static_assert(std::is_same_v<decltype(ref), microfmt::value_ref<const int>>);
  EXPECT_EQ(*ref, 19);
}

TEST(ValueRef, SupportsReadOnlyFormattingViews) {
  int value = 42;
  microfmt::hash_view<int> hash(value);

  std::variant<int> variant(value);
  microfmt::variant_view<int> view(variant);

  EXPECT_EQ(hash.compute_hash(), std::hash<int>{}(value));
  EXPECT_EQ(&view.get(), &variant);
}

TEST(ValuePtr, SupportsNullableBorrowedPointers) {
  microfmt::value_ptr<int> ptr;

  EXPECT_FALSE(ptr);
  EXPECT_EQ(ptr.get(), nullptr);

  int value = 42;
  ptr = microfmt::value_ptr<int>(&value);

  ASSERT_TRUE(ptr);
  EXPECT_EQ(ptr.get(), &value);
  EXPECT_EQ(*ptr, 42);

  *ptr = 7;
  EXPECT_EQ(value, 7);
}

TEST(ValuePtr, PreservesPointeeConversions) {
  derived_value value{{11}};
  microfmt::value_ptr<derived_value> derived(&value);
  microfmt::value_ptr<base_value> base(derived);
  microfmt::value_ptr<const void> erased(base);

  EXPECT_EQ(base.get(), static_cast<base_value *>(&value));
  EXPECT_EQ(base->value, 11);
  EXPECT_EQ(erased.get(), static_cast<const void *>(&value));
}

TEST(ValuePtr, DeductionGuidePreservesPointeeType) {
  const int value = 19;
  microfmt::value_ptr ptr(&value);

  static_assert(
      std::is_same_v<decltype(ptr), microfmt::value_ptr<const int>>);
  EXPECT_EQ(*ptr, 19);
}

} // namespace
