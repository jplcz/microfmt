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

} // namespace testing
} // namespace microfmt