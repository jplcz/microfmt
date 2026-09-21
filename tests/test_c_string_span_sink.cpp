#include <array>
#include <gtest/gtest.h>
#include <microfmt/sinks/c_string_span_sink.hpp>
#include <string_view>

namespace microfmt {
namespace testing {

TEST(CStringSpanSinkTest, BasicWrite) {
  char buffer[16];
  c_string_span_sink sink(buffer);

  EXPECT_EQ(sink.max_size(), 15);
  EXPECT_EQ(sink.size(), 0);
  EXPECT_STREQ(sink.c_str(), ""); // Initialized to empty string

  auto s = sink.as_sink();
  s.write("Hello");

  EXPECT_EQ(sink.size(), 5);
  EXPECT_STREQ(sink.c_str(), "Hello");
  EXPECT_EQ(sink.view(), "Hello");
}

TEST(CStringSpanSinkTest, SequentialWritesMoveNullTerminator) {
  char buffer[16];
  c_string_span_sink sink(buffer);
  auto s = sink.as_sink();

  s.write("Part 1");
  EXPECT_STREQ(sink.c_str(), "Part 1");

  s.write(" + 2");
  EXPECT_STREQ(sink.c_str(), "Part 1 + 2");
  EXPECT_EQ(sink.size(), 10);
}

TEST(CStringSpanSinkTest, ExactFit) {
  char buffer[6]; // Space for 5 chars + 1 null terminator
  c_string_span_sink sink(buffer);

  sink.as_sink().write("12345");

  EXPECT_EQ(sink.size(), 5);
  EXPECT_STREQ(sink.c_str(), "12345");
  EXPECT_EQ(sink.view(), "12345");
}

TEST(CStringSpanSinkTest, TruncationSafeguard) {
  char buffer[6]; // Space for 5 chars + 1 null terminator
  c_string_span_sink sink(buffer);

  sink.as_sink().write("1234567890"); // Attempt to overflow

  EXPECT_EQ(sink.size(), 5);
  EXPECT_EQ(sink.max_size(), 5);
  EXPECT_STREQ(sink.c_str(), "12345"); // Must truncate perfectly and maintain \0
}

TEST(CStringSpanSinkTest, EmptySpanHandling) {
  span<char> empty_buf;
  c_string_span_sink sink(empty_buf);

  EXPECT_EQ(sink.max_size(), 0);
  EXPECT_EQ(sink.size(), 0);

  // Calling c_str() on an empty span sink should safely return a static ""
  EXPECT_STREQ(sink.c_str(), "");

  // Writing to an empty span sink should be a safe no-op
  sink.as_sink().write("overflow");
  EXPECT_EQ(sink.size(), 0);
  EXPECT_STREQ(sink.c_str(), "");
}

TEST(CStringSpanSinkTest, SingleByteSpanHandling) {
  char buffer[1];  // Space only for the null terminator
  buffer[0] = 'X'; // Pre-fill with garbage to verify constructor neutralizes it

  c_string_span_sink sink(buffer);

  EXPECT_EQ(sink.max_size(), 0);
  EXPECT_STREQ(sink.c_str(), ""); // Should have written \0 immediately

  sink.as_sink().write("hello"); // Should safely discard
  EXPECT_EQ(sink.size(), 0);
  EXPECT_STREQ(sink.c_str(), "");
}

TEST(CStringSpanSinkTest, ResetBehavior) {
  char buffer[10];
  c_string_span_sink sink(buffer);
  auto s = sink.as_sink();

  s.write("data");
  EXPECT_EQ(sink.size(), 4);

  sink.reset();

  EXPECT_EQ(sink.size(), 0);
  EXPECT_STREQ(sink.c_str(), "");

  // Should be able to write again from the beginning
  s.write("new");
  EXPECT_EQ(sink.size(), 3);
  EXPECT_STREQ(sink.c_str(), "new");
}

#if RELOCO_HAS_STD_SPAN
TEST(CStringSpanSinkTest, StdSpanConstructor) {
  std::array<char, 8> buffer;
  std::span<char, 8> std_buf(buffer);

  c_string_span_sink sink(std_buf);

  sink.as_sink().write("std");

  EXPECT_EQ(sink.size(), 3);
  EXPECT_STREQ(sink.c_str(), "std");
}
#endif

} // namespace testing
} // namespace microfmt