// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/inspector/concrete_metadata_map.hpp>
#include <microfmt/inspector/metadata_map.hpp>

#include <type_traits>
#include <utility>

namespace {

void print_int(const void *ptr, const microfmt::sink &out) noexcept {
  microfmt::formatter<int> fmt;
  fmt.format(*static_cast<const int *>(ptr), out);
}

struct generator_state {
  const microfmt::property_entry *entries;
  std::size_t size;
  std::size_t index;
};

bool next_property(void *ctx, microfmt::property_entry &out) noexcept {
  auto &state = *static_cast<generator_state *>(ctx);
  if (state.index >= state.size) {
    return false;
  }
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  out = state.entries[state.index++];
  RELOCO_END_UNSAFE_BUFFER_USAGE;
  return true;
}

template <typename T, typename = void>
struct can_make_metadata_view_from_rvalue : std::false_type {};

template <typename T>
struct can_make_metadata_view_from_rvalue<
    T, std::void_t<decltype(std::declval<const T &&>().make_view(
           std::declval<typename T::span_iteration_state &>()))>>
    : std::true_type {};

static_assert(
    !can_make_metadata_view_from_rvalue<microfmt::concrete_metadata_map>::value);

TEST(PropertyEntry, FormatsValueOrNull) {
  const int value = 42;
  const microfmt::property_entry present{"answer", &value, print_int};
  const microfmt::property_entry missing{};

  microfmt::buffer_sink<16> output;
  present.format_value(output.as_sink());
  missing.format_value(output.as_sink());

  EXPECT_EQ(output.view(), "42null");
}

TEST(MetadataMap, RejectsIncompleteGenerator) {
  generator_state state{nullptr, 0, 0};
  microfmt::property_entry entry;

  EXPECT_FALSE(microfmt::metadata_map(nullptr, next_property).get_next(entry));
  EXPECT_FALSE(microfmt::metadata_map(&state, nullptr).get_next(entry));
}

TEST(MetadataMap, IteratesAndFormatsGeneratedEntries) {
  const int first = 7;
  const int second = 11;
  const microfmt::property_entry entries[]{
      {"first", &first, print_int},
      {"second", &second, print_int},
      {"missing", nullptr, nullptr},
  };
  generator_state state{entries, 3, 0};
  const microfmt::metadata_map map(&state, next_property);

  EXPECT_EQ(microfmt::format<64>("{}", map).view(),
            "{\"first\": 7, \"second\": 11, \"missing\": null}");

  microfmt::property_entry entry;
  EXPECT_FALSE(map.get_next(entry));
}

TEST(ConcreteMetadataMap, TracksCapacityAndRejectsOverflow) {
  microfmt::property_entry storage[2];
  microfmt::concrete_metadata_map map(storage);
  int first = 1;
  int second = 2;
  int overflow = 3;

  EXPECT_TRUE(map.empty());
  EXPECT_EQ(map.size(), 0U);
  EXPECT_EQ(map.capacity(), 2U);
  EXPECT_TRUE(map.set("first", microfmt::value_ref(first)));
  EXPECT_TRUE(map.set("second", microfmt::value_ref(second)));
  EXPECT_FALSE(map.set("overflow", microfmt::value_ref(overflow)));
  EXPECT_EQ(map.size(), 2U);
}

TEST(ConcreteMetadataMap, UpdatesExistingKeyAndValueType) {
  microfmt::property_entry storage[1];
  microfmt::concrete_metadata_map map(storage);
  int initial = 5;
  const microfmt::string_view replacement = "ready";

  ASSERT_TRUE(map.set("state", microfmt::value_ref(initial)));
  ASSERT_TRUE(map.set("state", microfmt::value_ref(replacement)));
  EXPECT_EQ(map.size(), 1U);

  microfmt::concrete_metadata_map::span_iteration_state state;
  EXPECT_EQ(microfmt::format<32>("{}", map.make_view(state)).view(),
            "{\"state\": ready}");
}

TEST(ConcreteMetadataMap, BorrowsValuesAndResetsIterationState) {
  microfmt::property_entry storage[2];
  microfmt::concrete_metadata_map map(storage);
  int count = 4;
  bool active = true;

  ASSERT_TRUE(map.set("count", microfmt::value_ref(count)));
  ASSERT_TRUE(map.set("active", microfmt::value_ref(active)));

  microfmt::concrete_metadata_map::span_iteration_state state;
  EXPECT_EQ(microfmt::format<48>("{}", map.make_view(state)).view(),
            "{\"count\": 4, \"active\": true}");

  count = 9;
  active = false;
  EXPECT_EQ(microfmt::format<48>("{}", map.make_view(state)).view(),
            "{\"count\": 9, \"active\": false}");
}

TEST(ConcreteMetadataMap, FormatsEmptyStorage) {
  microfmt::concrete_metadata_map map(
      microfmt::span<microfmt::property_entry>{});
  microfmt::concrete_metadata_map::span_iteration_state state;

  EXPECT_EQ(microfmt::format<8>("{}", map.make_view(state)).view(), "{}");
}

} // namespace
