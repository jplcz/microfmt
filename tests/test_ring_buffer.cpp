#include <gtest/gtest.h>
#include <microfmt/microfmt.hpp>
#include <microfmt/ring_buffer_sink.hpp>

TEST(RingBufferSinkTest, LinearWritingBeforeWrap) {
  microfmt::ring_buffer_sink<16> rb;
  auto out = rb.as_sink();

  microfmt::format_to(out, "Hello {:d}", 42);

  EXPECT_EQ(rb.size(), 8);
  EXPECT_FALSE(rb.full());

  const auto slices = rb.view();
  EXPECT_EQ(slices.first, "Hello 42");
  EXPECT_TRUE(slices.second.empty());
}

TEST(RingBufferSinkTest, OverwriteAndWrapping) {
  // Capacity: 8 bytes
  microfmt::ring_buffer_sink<8> rb;
  auto out = rb.as_sink();

  // Write 12 bytes -> First 4 overwritten
  microfmt::format_to(out, "ABCDEFGHIJKL");

  EXPECT_EQ(rb.size(), 8);
  EXPECT_TRUE(rb.full());

  // Dump chronologically to a buffer
  microfmt::buffer_sink<32> dump;
  rb.dump_to(dump.as_sink());

  // Should contain only the last 8 bytes: "EFGHIJKL"
  EXPECT_EQ(dump.view(), "EFGHIJKL");
}

TEST(RingBufferSinkTest, SlicesSplitCheck) {
  microfmt::ring_buffer_sink<8> rb;
  auto out = rb.as_sink();

  // Write 10 chars: 0123456789 -> Retained: 23456789
  microfmt::format_to(out, "0123456789");

  const auto slices = rb.view();
  EXPECT_EQ(slices.first, "234567"); // Tail to end
  EXPECT_EQ(slices.second, "89");    // Start to head
}
