// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/detail/tls_provider.hpp>

#include <thread>
#include <type_traits>

namespace {

// Distinct tag types keep each test's thread-local slot isolated from the
// others, since microfmt::tls_state<T, Tag> is differentiated by both T and
// Tag.
struct roundtrip_tag {};
struct default_value_tag {};
struct distinct_tag_a {};
struct distinct_tag_b {};
struct thread_isolation_tag {};
struct type_distinction_tag_int {};
struct type_distinction_tag_double {};
struct provider_direct_tag {};
struct provider_alias_tag {};

} // namespace

TEST(TlsProvider, DefaultsToNullptr) {
  using tls = microfmt::tls_state<int, default_value_tag>;
  EXPECT_EQ(tls::get(), nullptr);
}

TEST(TlsProvider, SetIsVisibleThroughGet) {
  using tls = microfmt::tls_state<int, roundtrip_tag>;

  int value = 42;
  tls::set(&value);
  EXPECT_EQ(tls::get(), &value);

  tls::set(nullptr);
  EXPECT_EQ(tls::get(), nullptr);
}

TEST(TlsProvider, DistinctTagsAreIndependent) {
  using tls_a = microfmt::tls_state<int, distinct_tag_a>;
  using tls_b = microfmt::tls_state<int, distinct_tag_b>;

  int value_a = 1;
  int value_b = 2;

  tls_a::set(&value_a);
  tls_b::set(&value_b);

  EXPECT_EQ(tls_a::get(), &value_a);
  EXPECT_EQ(tls_b::get(), &value_b);

  tls_a::set(nullptr);
  tls_b::set(nullptr);
}

TEST(TlsProvider, DistinctTypesAreIndependentForSameTag) {
  using tls_int = microfmt::tls_state<int, type_distinction_tag_int>;
  using tls_double = microfmt::tls_state<double, type_distinction_tag_double>;

  int int_value = 7;
  double double_value = 3.5;

  tls_int::set(&int_value);
  tls_double::set(&double_value);

  EXPECT_EQ(tls_int::get(), &int_value);
  EXPECT_EQ(tls_double::get(), &double_value);

  tls_int::set(nullptr);
  tls_double::set(nullptr);
}

TEST(TlsProvider, EachThreadHasAnIndependentSlot) {
  using tls = microfmt::tls_state<int, thread_isolation_tag>;

  int main_value = 100;
  tls::set(&main_value);

  int *worker_initial_value = nullptr;
  int *worker_value_after_set = nullptr;
  int worker_value = 200;

  std::thread worker([&] {
    // The worker thread must start with its own, unset slot: it must not
    // observe the pointer the main thread just stored.
    worker_initial_value = tls::get();
    tls::set(&worker_value);
    worker_value_after_set = tls::get();
  });
  worker.join();

  EXPECT_EQ(worker_initial_value, nullptr);
  EXPECT_EQ(worker_value_after_set, &worker_value);
  // The main thread's own slot must be unaffected by the worker thread's set().
  EXPECT_EQ(tls::get(), &main_value);

  tls::set(nullptr);
}

TEST(TlsProvider, UnderlyingDefaultProviderRoundTripsDirectly) {
  using provider = microfmt::detail::default_tls_provider<int, provider_direct_tag>;

  int value = 9;

  EXPECT_EQ(provider::get(), nullptr);

  provider::set(&value);
  EXPECT_EQ(provider::get(), &value);

  provider::set(nullptr);
  EXPECT_EQ(provider::get(), nullptr);
}

TEST(TlsProvider, TlsStateProviderAliasMatchesDefaultProvider) {
  using tls = microfmt::tls_state<int, provider_alias_tag>;
  using expected_provider = microfmt::detail::default_tls_provider<int, provider_alias_tag>;
  static_assert(std::is_same_v<tls::provider, expected_provider>);
}
