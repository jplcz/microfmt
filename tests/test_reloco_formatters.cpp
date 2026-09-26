// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/formatters/reloco.hpp>
#include <microfmt/microfmt.hpp>
#include <reloco/binary_heap.hpp>
#include <reloco/boxed_slice.hpp>
#include <reloco/checked.hpp>
#include <reloco/cow.hpp>
#include <reloco/duration.hpp>
#include <reloco/flat_hash_map.hpp>
#include <reloco/flat_hash_set.hpp>
#include <reloco/flat_map.hpp>
#include <reloco/flat_set.hpp>
#include <reloco/inline_flat_map.hpp>
#include <reloco/inline_flat_set.hpp>
#include <reloco/inline_string.hpp>
#include <reloco/inline_vec_deque.hpp>
#include <reloco/inline_vector.hpp>
#include <reloco/instant.hpp>
#include <reloco/non_zero.hpp>
#include <reloco/ordering.hpp>
#include <reloco/outline_vec_deque.hpp>
#include <reloco/outline_vector.hpp>
#include <reloco/rc.hpp>
#include <reloco/saturating.hpp>
#include <reloco/shared_ptr.hpp>
#include <reloco/sso_flat_map.hpp>
#include <reloco/sso_flat_set.hpp>
#include <reloco/sso_string.hpp>
#include <reloco/sso_vec_deque.hpp>
#include <reloco/sso_vector.hpp>
#include <reloco/string.hpp>
#include <reloco/type_id.hpp>
#include <reloco/unique_ptr.hpp>
#include <reloco/vec_deque.hpp>
#include <reloco/vector.hpp>
#include <reloco/wrapping.hpp>

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

TEST(RelocoFormattersTest, FormatsBoxedSlice) {
  auto created = reloco::boxed_slice<int>::try_create(3, 0);
  ASSERT_TRUE(created.has_value());
  auto slice = std::move(created.value());
  slice[0] = 1;
  slice[1] = 2;
  slice[2] = 3;

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", slice);
  EXPECT_EQ(buffer.view(), "[1, 2, 3]");
}

TEST(RelocoFormattersTest, FormatsBinaryHeap) {
  auto created = reloco::binary_heap<int>::try_create();
  ASSERT_TRUE(created.has_value());
  auto heap = std::move(created.value());
  ASSERT_TRUE(heap.try_push(5));
  ASSERT_TRUE(heap.try_push(1));
  ASSERT_TRUE(heap.try_push(9));

  // A max-heap's begin()/end() walk unspecified heap order, not sorted
  // order, so this test only checks the greatest element leads.
  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", heap);
  EXPECT_EQ(buffer.view()[1], '9');
  EXPECT_EQ(buffer.view().front(), '[');
  EXPECT_EQ(buffer.view().back(), ']');
}

TEST(RelocoFormattersTest, FormatsSsoVector) {
  reloco::sso_vector<int, 4> vec;
  ASSERT_TRUE(vec.try_push_back(1));
  ASSERT_TRUE(vec.try_push_back(2));

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", vec);
  EXPECT_EQ(buffer.view(), "[1, 2]");
}

TEST(RelocoFormattersTest, FormatsSsoFlatSet) {
  reloco::sso_flat_set<int, 4> set;
  ASSERT_TRUE(set.try_insert(5).has_value());
  ASSERT_TRUE(set.try_insert(2).has_value());

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", set);
  EXPECT_EQ(buffer.view(), "[2, 5]");
}

TEST(RelocoFormattersTest, FormatsSsoFlatMap) {
  reloco::sso_flat_map<int, int, 4> map;
  ASSERT_TRUE(map.try_insert(2, 20).has_value());
  ASSERT_TRUE(map.try_insert(1, 10).has_value());

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", map);
  EXPECT_EQ(buffer.view(), "{1: 10, 2: 20}");
}

TEST(RelocoFormattersTest, FormatsOutlineVector) {
  alignas(alignof(int)) std::byte storage[3 * sizeof(int)];
  reloco::outline_vector<int> vec(reloco::span<std::byte>(storage, sizeof(storage)));
  ASSERT_TRUE(vec.try_push_back(1));
  ASSERT_TRUE(vec.try_push_back(2));
  ASSERT_TRUE(vec.try_push_back(3));

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", vec);
  EXPECT_EQ(buffer.view(), "[1, 2, 3]");
}

TEST(RelocoFormattersTest, FormatsVecDeque) {
  auto created = reloco::vec_deque<int>::try_create();
  ASSERT_TRUE(created.has_value());
  reloco::vec_deque<int> deque = std::move(created.value());
  ASSERT_TRUE(deque.try_push_back(1).has_value());
  ASSERT_TRUE(deque.try_push_front(0).has_value());
  ASSERT_TRUE(deque.try_push_back(2).has_value());

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", deque);
  EXPECT_EQ(buffer.view(), "[0, 1, 2]");
}

TEST(RelocoFormattersTest, FormatsSsoVecDeque) {
  reloco::sso_vec_deque<int, 4> deque;
  ASSERT_TRUE(deque.try_push_back(1).has_value());
  ASSERT_TRUE(deque.try_push_front(0).has_value());
  ASSERT_TRUE(deque.try_push_back(2).has_value());

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", deque);
  EXPECT_EQ(buffer.view(), "[0, 1, 2]");
}

TEST(RelocoFormattersTest, FormatsInlineVecDeque) {
  reloco::inline_vec_deque<int, 4> deque;
  ASSERT_TRUE(deque.try_push_back(1).has_value());
  ASSERT_TRUE(deque.try_push_front(0).has_value());
  ASSERT_TRUE(deque.try_push_back(2).has_value());

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", deque);
  EXPECT_EQ(buffer.view(), "[0, 1, 2]");
}

TEST(RelocoFormattersTest, FormatsOutlineVecDeque) {
  alignas(alignof(int)) std::byte storage[3 * sizeof(int)];
  reloco::outline_vec_deque<int> deque(reloco::span<std::byte>(storage, sizeof(storage)));
  ASSERT_TRUE(deque.try_push_back(1).has_value());
  ASSERT_TRUE(deque.try_push_front(0).has_value());
  ASSERT_TRUE(deque.try_push_back(2).has_value());

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", deque);
  EXPECT_EQ(buffer.view(), "[0, 1, 2]");
}

TEST(RelocoFormattersTest, FormatsFlatHashSet) {
  reloco::flat_hash_set<int> set;
  ASSERT_TRUE(set.try_insert(1).has_value());
  ASSERT_TRUE(set.try_insert(2).has_value());
  ASSERT_TRUE(set.try_insert(3).has_value());

  // Bucket order is unspecified, so just check that every element made it
  // into the output rather than asserting an exact string.
  microfmt::buffer_sink<64> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", set);
  const auto view = buffer.view();
  EXPECT_FALSE(view.empty());
  EXPECT_EQ(view.front(), '[');
  EXPECT_EQ(view.back(), ']');
  EXPECT_NE(view.find("1"), reloco::basic_string_view<char>::npos);
  EXPECT_NE(view.find("2"), reloco::basic_string_view<char>::npos);
  EXPECT_NE(view.find("3"), reloco::basic_string_view<char>::npos);
}

TEST(RelocoFormattersTest, FormatsFlatHashMap) {
  reloco::flat_hash_map<int, int> map;
  ASSERT_TRUE(map.try_insert(1, 10).has_value());
  ASSERT_TRUE(map.try_insert(2, 20).has_value());

  // Bucket order is unspecified, so just check that every entry made it
  // into the output rather than asserting an exact string.
  microfmt::buffer_sink<64> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", map);
  const auto view = buffer.view();
  EXPECT_FALSE(view.empty());
  EXPECT_EQ(view.front(), '{');
  EXPECT_EQ(view.back(), '}');
  EXPECT_NE(view.find("1: 10"), reloco::basic_string_view<char>::npos);
  EXPECT_NE(view.find("2: 20"), reloco::basic_string_view<char>::npos);
}

TEST(RelocoFormattersTest, FormatsRcAndWeakRc) {
  auto created = reloco::try_create_combined_rc<int>(8);
  ASSERT_TRUE(created.has_value());
  reloco::rc<int> shared = std::move(created.value());
  const reloco::weak_rc<int> weak(shared);

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

TEST(RelocoFormattersTest, FormatsRcNull) {
  const reloco::rc<int> empty;

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", empty);
  EXPECT_EQ(buffer.view(), "(null)");
}

TEST(RelocoFormattersTest, FormatsCowBorrowedAndOwned) {
  int original = 42;
  reloco::cow<int> borrowed(original);
  reloco::cow<int> owned(99);

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", borrowed);
  EXPECT_EQ(buffer.view(), "42");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{}", owned);
  EXPECT_EQ(buffer.view(), "99");
}

TEST(RelocoFormattersTest, FormatsNonZero) {
  auto created = reloco::non_zero<int>::try_create(7);
  ASSERT_TRUE(created.has_value());

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", created.value());
  EXPECT_EQ(buffer.view(), "7");
}

TEST(RelocoFormattersTest, FormatsSaturating) {
  const reloco::saturating<std::uint8_t> value(200);

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", value);
  EXPECT_EQ(buffer.view(), "200");
}

TEST(RelocoFormattersTest, FormatsWrapping) {
  const reloco::wrapping<std::uint8_t> value(250);

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", value);
  EXPECT_EQ(buffer.view(), "250");
}

TEST(RelocoFormattersTest, FormatsChecked) {
  const reloco::checked<int> value(13);

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", value);
  EXPECT_EQ(buffer.view(), "13");
}

TEST(RelocoFormattersTest, FormatsOrdering) {
  microfmt::buffer_sink<32> buffer;

  microfmt::format_to(buffer.as_sink(), "{}", reloco::ordering::less);
  EXPECT_EQ(buffer.view(), "less");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{}", reloco::ordering::equal);
  EXPECT_EQ(buffer.view(), "equal");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{}", reloco::ordering::greater);
  EXPECT_EQ(buffer.view(), "greater");
}

TEST(RelocoFormattersTest, FormatsTypeId) {
  microfmt::buffer_sink<32> buffer;

  // int has a built-in RELOCO_TYPE_ID_NAME registration.
  microfmt::format_to(buffer.as_sink(), "{}", reloco::type_id::of<int>());
  EXPECT_EQ(buffer.view(), "int");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{}", reloco::type_id{});
  EXPECT_EQ(buffer.view(), "<no type>");

  buffer.reset();
  struct unnamed_test_type {};
  microfmt::format_to(buffer.as_sink(), "{}", reloco::type_id::of<unnamed_test_type>());
  EXPECT_EQ(buffer.view(), "<unnamed type>");
}

TEST(RelocoFormattersTest, FormatsDuration) {
  microfmt::buffer_sink<32> buffer;

  microfmt::format_to(buffer.as_sink(), "{}", reloco::duration::from_nanos(1'234'567'890ULL));
  EXPECT_EQ(buffer.view(), "1.234567890s");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{:m}", reloco::duration::from_millis(1'500ULL));
  EXPECT_EQ(buffer.view(), "1.500s");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{:u}", reloco::duration::from_micros(2'000'250ULL));
  EXPECT_EQ(buffer.view(), "2.000250s");

  buffer.reset();
  microfmt::format_to(buffer.as_sink(), "{:r}", reloco::duration::from_secs(5));
  EXPECT_EQ(buffer.view(), "5.000000000");
}

TEST(RelocoFormattersTest, FormatsInstant) {
  microfmt::buffer_sink<32> buffer;

  auto zero = reloco::instant();
  auto later = zero + reloco::duration::from_millis(3'661'250ULL); // 1h 1m 1.25s
  microfmt::format_to(buffer.as_sink(), "{}", later);
  EXPECT_EQ(buffer.view(), "01:01:01.250");
}
