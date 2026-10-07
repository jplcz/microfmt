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
#include <reloco/obfuscated_string.hpp>
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

TEST(RelocoFormattersTest, FormatsObfuscatedString) {
  static constexpr auto obfuscated = reloco::obfuscated_string("obfuscated-marker", UINT64_C(0x0123456789abcdef));

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", obfuscated);
  EXPECT_EQ(buffer.view(), "obfuscated-marker");
}

TEST(RelocoFormattersTest, FormatsObfuscatedStringDecryptedView) {
  auto view = RELOCO_OBFUSCATED_STR("already-decrypted");

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", view);
  EXPECT_EQ(buffer.view(), "already-decrypted");
}

TEST(RelocoFormattersTest, FormatsObfuscatedStringRef) {
  static constexpr auto obfuscated = reloco::obfuscated_string("type-erased-marker", UINT64_C(0xfedcba9876543210));
  const reloco::obfuscated_string_ref ref = obfuscated;

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", ref);
  EXPECT_EQ(buffer.view(), "type-erased-marker");
}

namespace {
RELOCO_DECLARE_OBFUSCATED_STR(g_test_formatter_marker);
RELOCO_DEFINE_OBFUSCATED_STR(g_test_formatter_marker, "declared-marker");
} // namespace

TEST(RelocoFormattersTest, FormatsDeclaredObfuscatedStringRef) {
  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", g_test_formatter_marker);
  EXPECT_EQ(buffer.view(), "declared-marker");
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

namespace {

// Deliberately has no microfmt::formatter<T> specialization: only
// reloco::Display<T>/reloco::Debug<T>, to prove container/wrapper
// formatters (reloco::vector<T>, reloco::unique_ptr<T>, ...) can still
// format elements/pointees that only opt into reloco's customization
// points, via microfmt::detail::element_formatter<T>.
struct display_only_point {
  int x;
  int y;
};

} // namespace

namespace reloco {
template <> struct Display<display_only_point> {
  static void format(const display_only_point &p, const microfmt::sink &out) noexcept {
    microfmt::format_to(out, "({}, {})", p.x, p.y);
  }
};
} // namespace reloco

TEST(RelocoFormattersTest, FormatsVectorOfDisplayOnlyElements) {
  reloco::vector<display_only_point> points;
  ASSERT_TRUE(points.try_push_back({1, 2}));
  ASSERT_TRUE(points.try_push_back({3, 4}));

  microfmt::buffer_sink<64> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", points);
  EXPECT_EQ(buffer.view(), "[(1, 2), (3, 4)]");
}

TEST(RelocoFormattersTest, FormatsUniquePtrOfDisplayOnlyElement) {
  auto created = reloco::unique_ptr<display_only_point>::try_create(display_only_point{5, 6});
  ASSERT_TRUE(created.has_value());
  reloco::unique_ptr<display_only_point> ptr = std::move(created.value());

  microfmt::buffer_sink<32> buffer;
  microfmt::format_to(buffer.as_sink(), "{}", ptr);
  EXPECT_EQ(buffer.view(), "(5, 6)");
}

#include <reloco/call_location.hpp>
#include <reloco/cell.hpp>
#include <reloco/error.hpp>
#include <reloco/external_vector.hpp>
#include <reloco/fixed_point.hpp>
#include <reloco/inline_vector.hpp>
#include <reloco/lru_cache.hpp>
#include <reloco/packed_bits.hpp>
#include <reloco/ring_buffer.hpp>
#include <reloco/tree_map.hpp>
#include <reloco/tree_set.hpp>
#include <reloco/variant.hpp>

TEST(RelocoFormattersTest, FormatsTreeSetAndMap) {
  auto set = reloco::tree_set<int>::try_create();
  ASSERT_TRUE(set);
  for (int v : {3, 1, 2}) {
    ASSERT_TRUE(set->try_insert(v));
  }
  EXPECT_EQ(microfmt::format_as<std::string>("{}", *set), "[1, 2, 3]");

  auto map = reloco::tree_map<int, int>::try_create();
  ASSERT_TRUE(map);
  ASSERT_TRUE(map->try_insert(2, 20));
  ASSERT_TRUE(map->try_insert(1, 10));
  EXPECT_EQ(microfmt::format_as<std::string>("{}", *map), "{1: 10, 2: 20}");
}

TEST(RelocoFormattersTest, FormatsExternalVectorAndRing) {
  int storage[4];
  reloco::external_vector<int> ev{reloco::span<int>(storage)};
  ASSERT_TRUE(ev.try_push_back(7));
  ASSERT_TRUE(ev.try_push_back(8));
  EXPECT_EQ(microfmt::format_as<std::string>("{}", ev), "[7, 8]");

  reloco::inline_ring_buffer<int, 4> ring;
  const int data[] = {1, 2, 3};
  ASSERT_TRUE(ring.try_write(reloco::span<const int>(data)));
  EXPECT_EQ(microfmt::format_as<std::string>("{}", ring), "[1, 2, 3]");
}

TEST(RelocoFormattersTest, FormatsMiscellaneousTypes) {
  EXPECT_EQ(microfmt::format_as<std::string>("{}", reloco::error::out_of_range), "out_of_range");
  EXPECT_EQ(microfmt::format_as<std::string>("{}", reloco::cell<int>(5)), "5");
  EXPECT_EQ(microfmt::format_as<std::string>("{}", reloco::call_location{"a.cpp", 12}), "a.cpp:12");
  EXPECT_EQ(microfmt::format_as<std::string>("{}", reloco::call_location_ref{}), "<no location>");
  EXPECT_EQ(microfmt::format_as<std::string>("{:x}", reloco::packed_bits<unsigned>(255u)), "ff");
  reloco::variant<int, bool> v{3};
  EXPECT_EQ(microfmt::format_as<std::string>("{}", v), "3");
  const auto fp = reloco::fixed_point<std::int32_t, 8>::from_raw(384);
  EXPECT_EQ(microfmt::format_as<std::string>("{:.1f}", fp), "1.5");
}

TEST(RelocoFormattersTest, FormatsWideAndFixedInts) {
  using i256 = reloco::fixed_int<256>;
  using u256 = reloco::fixed_uint<256>;
  i256 big = i256(1);
  for (int i = 0; i < 70; ++i) {
    big = big * i256(10);
  }
  EXPECT_EQ(microfmt::format_as<std::string>("{}", big), "1" + std::string(70, '0'));
  EXPECT_EQ(microfmt::format_as<std::string>("{}", -big), "-1" + std::string(70, '0'));
  EXPECT_EQ(microfmt::format_as<std::string>("{}", i256(0)), "0");
  EXPECT_EQ(microfmt::format_as<std::string>("{}", i256(-42)), "-42");
  EXPECT_EQ(microfmt::format_as<std::string>("{:#x}", u256(255)), "0xff");
  EXPECT_EQ(microfmt::format_as<std::string>("{:06}", i256(7)), "000007");
  EXPECT_EQ(microfmt::format_as<std::string>("{:x}", u256(1) << 128U), "1" + std::string(32, '0'));
  EXPECT_EQ(microfmt::format_as<std::string>("{}", ~u256(0)),
            "115792089237316195423570985008687907853269984665640564039457584007913129639935");

  const reloco::fixed_int<128> n = reloco::fixed_int<128>(1) << 100U;
  EXPECT_EQ(microfmt::format_as<std::string>("{}", n), "1267650600228229401496703205376");
  EXPECT_EQ(microfmt::format_as<std::string>("{}", reloco::fixed_uint<128>(0)), "0");
}
