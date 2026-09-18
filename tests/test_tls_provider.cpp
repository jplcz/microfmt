// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/detail/tls_provider.hpp>

#include <thread>
#include <type_traits>

namespace {

// Distinct tag types keep each test's thread-local slot isolated from the
// others, since microfmt::detail::tls_provider<T, Tag> is differentiated by
// both T and Tag.
struct default_value_tag {};
struct roundtrip_tag {};
struct distinct_tag_a {};
struct distinct_tag_b {};
struct thread_isolation_tag {};
struct type_distinction_tag_int {};
struct type_distinction_tag_double {};
struct reference_mutation_tag {};
struct pointer_default_tag {};
struct pointer_roundtrip_tag {};
struct pointer_thread_isolation_tag {};

} // namespace

TEST(TlsProvider, DefaultsToValueInitialized) {
  using tls = microfmt::detail::tls_provider<int, default_value_tag>;
  EXPECT_EQ(tls::get(), 0);
}

TEST(TlsProvider, SetIsVisibleThroughGet) {
  using tls = microfmt::detail::tls_provider<int, roundtrip_tag>;

  tls::set(42);
  EXPECT_EQ(tls::get(), 42);

  tls::set(0);
  EXPECT_EQ(tls::get(), 0);
}

TEST(TlsProvider, DistinctTagsAreIndependent) {
  using tls_a = microfmt::detail::tls_provider<int, distinct_tag_a>;
  using tls_b = microfmt::detail::tls_provider<int, distinct_tag_b>;

  tls_a::set(1);
  tls_b::set(2);

  EXPECT_EQ(tls_a::get(), 1);
  EXPECT_EQ(tls_b::get(), 2);

  tls_a::set(0);
  tls_b::set(0);
}

TEST(TlsProvider, DistinctTypesAreIndependentForSameTag) {
  using tls_int = microfmt::detail::tls_provider<int, type_distinction_tag_int>;
  using tls_double = microfmt::detail::tls_provider<double, type_distinction_tag_double>;

  tls_int::set(7);
  tls_double::set(3.5);

  EXPECT_EQ(tls_int::get(), 7);
  EXPECT_DOUBLE_EQ(tls_double::get(), 3.5);

  tls_int::set(0);
  tls_double::set(0.0);
}

TEST(TlsProvider, EachThreadHasAnIndependentSlot) {
  using tls = microfmt::detail::tls_provider<int, thread_isolation_tag>;

  tls::set(100);

  int worker_initial_value = -1;
  int worker_value_after_set = -1;

  std::thread worker([&] {
    // The worker thread must start with its own, default-initialized slot: it
    // must not observe the value the main thread just stored.
    worker_initial_value = tls::get();
    tls::set(200);
    worker_value_after_set = tls::get();
  });
  worker.join();

  EXPECT_EQ(worker_initial_value, 0);
  EXPECT_EQ(worker_value_after_set, 200);
  // The main thread's own slot must be unaffected by the worker thread's set().
  EXPECT_EQ(tls::get(), 100);

  tls::set(0);
}

TEST(TlsProvider, PointerSpecializationDefaultsToNullptr) {
  using tls = microfmt::detail::tls_provider<int *, pointer_default_tag>;
  EXPECT_EQ(tls::get(), nullptr);
}

TEST(TlsProvider, PointerSpecializationRoundTripsThroughSet) {
  using tls = microfmt::detail::tls_provider<int *, pointer_roundtrip_tag>;

  int value = 9;
  tls::set(&value);
  EXPECT_EQ(tls::get(), &value);

  tls::set(nullptr);
  EXPECT_EQ(tls::get(), nullptr);
}

TEST(TlsProvider, PointerSpecializationEachThreadHasAnIndependentSlot) {
  using tls = microfmt::detail::tls_provider<int *, pointer_thread_isolation_tag>;

  int main_value = 1;
  tls::set(&main_value);

  int *worker_initial_value = &main_value;
  int *worker_value_after_set = nullptr;
  int worker_value = 2;

  std::thread worker([&] {
    worker_initial_value = tls::get();
    tls::set(&worker_value);
    worker_value_after_set = tls::get();
  });
  worker.join();

  EXPECT_EQ(worker_initial_value, nullptr);
  EXPECT_EQ(worker_value_after_set, &worker_value);
  EXPECT_EQ(tls::get(), &main_value);

  tls::set(nullptr);
}
