#include <gtest/gtest.h>
#include <microfmt/sinks/memory_buffer.hpp>

namespace microfmt {
namespace testing {

TEST(MemoryBufferTest, FitsInInlineStorage) {
  memory_buffer<32> buf;
  auto sink = buf.as_sink();

  sink.write("Hello Inline");

  EXPECT_EQ(buf.size(), 12);
  EXPECT_EQ(buf.capacity(), 32); // Capacity shouldn't change
  EXPECT_EQ(buf.view(), "Hello Inline");

  // Verify it points to the internal stack array
  EXPECT_GE(buf.data(), reinterpret_cast<const char *>(&buf));
  EXPECT_LT(buf.data(), reinterpret_cast<const char *>(&buf) + sizeof(buf));
}

TEST(MemoryBufferTest, TransitionsToHeap) {
  memory_buffer<16> buf;
  auto sink = buf.as_sink();

  // Exactly 16 bytes - should fit perfectly inline
  sink.write("1234567890123456");
  EXPECT_EQ(buf.capacity(), 16);
  EXPECT_EQ(buf.size(), 16);

  // 17th byte - triggers the heap transition
  sink.write("!");

  EXPECT_GT(buf.capacity(), 16);
  EXPECT_EQ(buf.size(), 17);
  EXPECT_EQ(buf.view(), "1234567890123456!");

  // Verify pointer is no longer inside the object footprint
  EXPECT_TRUE(buf.data() < reinterpret_cast<const char *>(&buf) ||
              buf.data() > reinterpret_cast<const char *>(&buf) + sizeof(buf));
}

TEST(MemoryBufferTest, MultipleHeapReallocations) {
  memory_buffer<16> buf;
  auto sink = buf.as_sink();

  // Force multiple reallocations / expand_in_place calls
  for (int i = 0; i < 10; ++i) {
    sink.write("0123456789"); // 100 bytes total
  }

  EXPECT_EQ(buf.size(), 100);
  EXPECT_GE(buf.capacity(), 100);

  std::string expected;
  for (int i = 0; i < 10; ++i)
    expected += "0123456789";

  EXPECT_EQ(buf.view(), microfmt::string_view(expected));
}

TEST(MemoryBufferTest, FormatToIntegration) {
  memory_buffer<64> buf;

  // Test via format_to to ensure seamless back_inserter / sink behavior
  format_to(buf.as_sink(), "Testing {} {} {}", 1, 2, 3);

  EXPECT_EQ(buf.view(), "Testing 1 2 3");
}

TEST(MemoryBufferTest, StlIteratorsAndAlgorithms) {
  memory_buffer<32> buf;

  // Test push_back (enables back_inserter)
  std::string_view input = "cba";
  std::copy(input.begin(), input.end(), std::back_inserter(buf));

  EXPECT_EQ(buf.size(), 3);
  EXPECT_EQ(buf.view(), "cba");

  // Test iterators with std::sort
  std::sort(buf.begin(), buf.end());

  EXPECT_EQ(buf.view(), "abc");

  // Test Range-based for loop and element access
  std::string result;
  for (char c : buf) {
    result += c;
  }
  EXPECT_EQ(result, "abc");

  EXPECT_EQ(buf.front(), 'a');
  EXPECT_EQ(buf.back(), 'c');
}

TEST(MemoryBufferTest, MoveConstructInline) {
  memory_buffer<32> source;
  source.as_sink().write("stack data");

  // Move construct
  memory_buffer<32> dest(std::move(source));

  // Dest should have the data
  EXPECT_EQ(dest.size(), 10);
  EXPECT_EQ(dest.view(), "stack data");

  // Dest should point to ITS OWN inline buffer (inside its own memory footprint)
  EXPECT_GE(dest.data(), reinterpret_cast<const char *>(&dest));
  EXPECT_LT(dest.data(), reinterpret_cast<const char *>(&dest) + sizeof(dest));

  // Source should be reset
  EXPECT_EQ(source.size(), 0);
  EXPECT_GE(source.data(), reinterpret_cast<const char *>(&source));
  EXPECT_LT(source.data(), reinterpret_cast<const char *>(&source) + sizeof(source));
}

TEST(MemoryBufferTest, MoveConstructHeap) {
  memory_buffer<16> source;
  // Exceed 16 bytes to force heap allocation
  source.as_sink().write("this is a very long string on the heap");
  const char *heap_ptr = source.data();

  // Move construct
  memory_buffer<16> dest(std::move(source));

  EXPECT_EQ(dest.size(), 38);
  EXPECT_EQ(dest.view(), "this is a very long string on the heap");

  // Dest should have stolen the exact heap pointer
  EXPECT_EQ(dest.data(), heap_ptr);

  // Source should be reset to its inline buffer (inside its own memory footprint)
  EXPECT_EQ(source.size(), 0);
  EXPECT_GE(source.data(), reinterpret_cast<const char *>(&source));
  EXPECT_LT(source.data(), reinterpret_cast<const char *>(&source) + sizeof(source));
}

TEST(MemoryBufferTest, MoveAssignHeapToInline) {
  memory_buffer<16> source;
  source.as_sink().write("heap data longer than 16 bytes");

  memory_buffer<16> dest;
  dest.as_sink().write("short"); // currently on stack

  const char *heap_ptr = source.data();

  // Move assign
  dest = std::move(source);

  // Dest should steal the heap pointer
  EXPECT_EQ(dest.size(), 30);
  EXPECT_EQ(dest.view(), "heap data longer than 16 bytes");
  EXPECT_EQ(dest.data(), heap_ptr);

  // Source should reset to its inline buffer (inside its own memory footprint)
  EXPECT_EQ(source.size(), 0);
  EXPECT_GE(source.data(), reinterpret_cast<const char *>(&source));
  EXPECT_LT(source.data(), reinterpret_cast<const char *>(&source) + sizeof(source));
}

TEST(MemoryBufferTest, FormatAsCompatibility) {
  // Because it satisfies contiguous container requirements and has a default constructor,
  // format_as should seamlessly work with it!
  auto buf = format_as<memory_buffer<64>>("Hex: {:#x}", 255);

  EXPECT_EQ(buf.view(), "Hex: 0xff");
}

} // namespace testing
} // namespace microfmt