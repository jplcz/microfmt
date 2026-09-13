#include <gtest/gtest.h>
#include <microfmt/microfmt.hpp>

#include <cstdint>
#include <cstring>
#include <string_view>

// ============================================================================
// span Tests (C++17 Replacement View & C++20 std::span Interop)
// ============================================================================

TEST(CoreSpan, DefaultAndPointerConstruction) {
  microfmt::span<char> empty_span;
  EXPECT_TRUE(empty_span.empty());
  EXPECT_EQ(empty_span.size(), 0u);
  EXPECT_EQ(empty_span.data(), nullptr);

  constexpr char raw_arr[] = "0123456789abcdef";
  microfmt::span<const char> arr_span(raw_arr, sizeof(raw_arr) - 1);
  EXPECT_FALSE(arr_span.empty());
  EXPECT_EQ(arr_span.size(), 16u);
  EXPECT_EQ(arr_span.data(), raw_arr);
  EXPECT_EQ(arr_span[0], '0');
  EXPECT_EQ(arr_span[15], 'f');
}

TEST(CoreSpan, ArrayDeductionAndIterators) {
  int nums[] = {10, 20, 30, 40};
  microfmt::span<int> s(nums);

  EXPECT_EQ(s.size(), 4u);
  EXPECT_EQ(*s.begin(), 10);
  EXPECT_EQ(*(s.end() - 1), 40);

  size_t count = 0;
  for (int n : s) {
    count += static_cast<size_t>(n);
  }
  EXPECT_EQ(count, 100u);
}

#if MICROFMT_HAS_STD_SPAN
TEST(CoreSpan, StdSpanInteroperability) {
  char data[] = "interop_test";
  std::span<char> std_s(data, sizeof(data));

  // Implicit construct from std::span
  microfmt::span<char> custom_s(std_s);
  EXPECT_EQ(custom_s.size(), std_s.size());
  EXPECT_EQ(custom_s.data(), std_s.data());

  // Implicit conversion to std::span
  std::span<char> roundtrip = custom_s;
  EXPECT_EQ(roundtrip.data(), data);
}
#endif

// ============================================================================
// Concrete Sink Implementations & Bounds Verification
// ============================================================================

TEST(CoreSink, BufferSinkStorageAndReset) {
  microfmt::buffer_sink<32> buf;
  EXPECT_EQ(buf.size(), 0u);
  EXPECT_TRUE(buf.view().empty());

  microfmt::format_to(buf.as_sink(), "Hello {}", 42);
  EXPECT_EQ(buf.view(), "Hello 42");
  EXPECT_EQ(buf.size(), 8u);

  buf.reset();
  EXPECT_EQ(buf.size(), 0u);
  EXPECT_TRUE(buf.view().empty());
}

TEST(CoreSink, SpanSinkTruncatesAtCapacity) {
  char memory[10];
  microfmt::span_sink sink(microfmt::span<char>(memory, sizeof(memory)));

  // Writing 14 characters into a 10-byte sink
  microfmt::format_to(sink.as_sink(), "123456789ABC");

  EXPECT_EQ(sink.size(), 10u);
  EXPECT_EQ(sink.view(), "123456789A");
}

TEST(CoreSink, IteratorSinkWithRawPointer) {
  char raw[32];
  auto end_ptr = microfmt::format_to(raw, "Value = 0x{:x}", 0xDEAD);
  *end_ptr = '\0';

  EXPECT_STREQ(raw, "Value = 0xdead");
}

TEST(CoreSink, TypeErasedCustomCallbackSink) {
  size_t write_call_count = 0;
  size_t total_bytes = 0;

  struct TestContext {
    size_t *calls;
    size_t *bytes;
  } ctx{&write_call_count, &total_bytes};

  microfmt::sink custom_sink{&ctx,
                             [](void *context, std::string_view sv) noexcept {
                               auto *c = static_cast<TestContext *>(context);
                               (*c->calls)++;
                               *c->bytes += sv.size();
                             }};

  microfmt::format_to(custom_sink, "A = {}, B = {}", 1, "test");
  EXPECT_GT(write_call_count, 0u);
  EXPECT_EQ(total_bytes, 15u); // "A = 1, B = test"
}

// ============================================================================
// Formatter Core: Primitives, Radix, Specifiers & Escaping
// ============================================================================

TEST(CoreFormat, IntegerRadixAndWidth) {
  // Decimal (signed and unsigned)
  EXPECT_EQ(microfmt::format<32>("{}", 0).view(), "0");
  EXPECT_EQ(microfmt::format<32>("{}", -1).view(), "-1");
  EXPECT_EQ(microfmt::format<32>("{}", 2147483647).view(), "2147483647");
  EXPECT_EQ(microfmt::format<32>("{}", -2147483648LL).view(), "-2147483648");
  EXPECT_EQ(microfmt::format<32>("{}", 18446744073709551615ULL).view(),
            "18446744073709551615");

  // Hex lowercase and uppercase
  EXPECT_EQ(microfmt::format<32>("{:x}", 0xbeef).view(), "beef");
  EXPECT_EQ(microfmt::format<32>("{:X}", 0xbeef).view(), "BEEF");

  // Hex with zero padding
  EXPECT_EQ(microfmt::format<32>("{:04x}", 0x1a).view(), "001a");
  EXPECT_EQ(microfmt::format<32>("{:08X}", 0x1a2b).view(), "00001A2B");
  EXPECT_EQ(microfmt::format<32>("{:016x}", 0xffff800000000000ULL).view(),
            "ffff800000000000");
}

TEST(CoreFormat, CharAndBoolOutput) {
  EXPECT_EQ(microfmt::format<16>("{}", 'Z').view(), "Z");
  EXPECT_EQ(microfmt::format<16>("{} / {}", true, false).view(),
            "true / false");
}

TEST(CoreFormat, StringAndNullStringHandling) {
  const char *valid_str = "embedded";
  const char *null_str = nullptr;
  std::string_view sv = "system";

  EXPECT_EQ(microfmt::format<32>("{} {}", valid_str, sv).view(),
            "embedded system");
  EXPECT_EQ(microfmt::format<32>("ptr: {}", null_str).view(), "ptr: (null)");
}

TEST(CoreFormat, PointerFormatting) {
  void *null_p = nullptr;
  EXPECT_EQ(microfmt::format<32>("{}", null_p).view(), "0x0");

  const void *addr = reinterpret_cast<const void *>(0x1000);
  EXPECT_EQ(microfmt::format<32>("{}", addr).view(), "0x1000");
  EXPECT_EQ(microfmt::format<32>("{:08}", addr).view(), "0x00001000");
}

TEST(CoreFormat, EscapeBraceSequences) {
  EXPECT_EQ(microfmt::format<32>("{{").view(), "{");
  EXPECT_EQ(microfmt::format<32>("}}").view(), "}");
  EXPECT_EQ(microfmt::format<32>("{{{}}}", 42).view(), "{42}");
  EXPECT_EQ(microfmt::format<32>("{{noparam}}").view(), "{noparam}");
}

TEST(CoreFormat, ArgumentMismatchSafety) {
  // Missing arguments
  EXPECT_EQ(microfmt::format<32>("A: {}, B: {}", 1).view(),
            "A: 1, B: {MISSING}");

  // Extra unused arguments should format without crashing
  EXPECT_EQ(microfmt::format<32>("Only: {}", 10, 20, 30).view(), "Only: 10");

  // Empty format string with arguments
  EXPECT_EQ(microfmt::format<32>("", 10, 20).view(), "");
}

// ============================================================================
// Custom Type Specialization via formatter<T>
// ============================================================================

struct Point2D {
  int x;
  int y;
};

template <> struct microfmt::formatter<Point2D> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const Point2D &p, const sink &out) const noexcept {
    microfmt::format_to(out, "Point({}, {})", p.x, p.y);
  }
};

TEST(CoreCustomType, SpecializationFormat) {
  Point2D pt{12, -34};
  auto res = microfmt::format<32>("Position: {}", pt);
  EXPECT_EQ(res.view(), "Position: Point(12, -34)");
}

// ============================================================================
// Integer Boundary, Sign, and Radix Edge Cases
// ============================================================================

TEST(CoreFormat, IntegerLimitsAndBoundaries) {
  // 8-bit, 16-bit, 32-bit, 64-bit edge boundaries
  int8_t min_i8 = -128;
  int8_t max_i8 = 127;
  uint8_t max_u8 = 255;
  EXPECT_EQ(microfmt::format<16>("{}", min_i8).view(), "-128");
  EXPECT_EQ(microfmt::format<16>("{}", max_i8).view(), "127");
  EXPECT_EQ(microfmt::format<16>("{}", max_u8).view(), "255");

  int16_t min_i16 = -32768;
  int16_t max_i16 = 32767;
  EXPECT_EQ(microfmt::format<16>("{}", min_i16).view(), "-32768");
  EXPECT_EQ(microfmt::format<16>("{}", max_i16).view(), "32767");

  // Exact int64 limits
  int64_t min_i64 = -9223372036854775807LL - 1LL;
  uint64_t max_u64 = 0xFFFFFFFFFFFFFFFFULL;
  EXPECT_EQ(microfmt::format<32>("{}", min_i64).view(), "-9223372036854775808");
  EXPECT_EQ(microfmt::format<32>("0x{:x}", max_u64).view(),
            "0xffffffffffffffff");
  EXPECT_EQ(microfmt::format<32>("0x{:X}", max_u64).view(),
            "0xFFFFFFFFFFFFFFFF");
}

TEST(CoreFormat, HexPaddingEdgeCases) {
  // Exact width equal to length
  EXPECT_EQ(microfmt::format<16>("{:04x}", 0x1234).view(), "1234");

  // Width smaller than number of digits (should not truncate the number)
  EXPECT_EQ(microfmt::format<16>("{:02x}", 0x1234).view(), "1234");

  // Zero with padding
  EXPECT_EQ(microfmt::format<16>("{:04x}", 0).view(), "0000");
  EXPECT_EQ(microfmt::format<16>("{:01x}", 0).view(), "0");
}

// ============================================================================
// String Literal, Decay, and Null Character Handling
// ============================================================================

TEST(CoreFormat, StringLiteralAndDecayTypes) {
  // Direct const char[N] literals
  EXPECT_EQ(microfmt::format<32>("Literal: {}", "const literal").view(),
            "Literal: const literal");

  // Mutable char array
  char mut_arr[] = "mutable";
  EXPECT_EQ(microfmt::format<32>("{}", mut_arr).view(), "mutable");

  // Empty string literals
  EXPECT_EQ(microfmt::format<16>("{}", "").view(), "");
  EXPECT_EQ(microfmt::format<16>("<{}>", "").view(), "<>");

  // Const char* variable
  const char *str_ptr = "pointer";
  EXPECT_EQ(microfmt::format<16>("{}", str_ptr).view(), "pointer");
}

TEST(CoreFormat, ConsecutiveAndAdjacentTokens) {
  // Back-to-back placeholders without delimiters
  EXPECT_EQ(microfmt::format<32>("{}{}{}", 1, 2, 3).view(), "123");
  EXPECT_EQ(microfmt::format<32>("{:02x}{:02x}{:02x}", 0x1, 0xA, 0xFF).view(),
            "010aff");

  // Interleaved escaped braces and placeholders
  EXPECT_EQ(microfmt::format<32>("{{{}}}{{{}}}", "A", "B").view(), "{A}{B}");
  EXPECT_EQ(microfmt::format<32>("{{{:02x}}}", 5).view(), "{05}");
}

// ============================================================================
// Buffer and Sink Stress/Boundary Conditions
// ============================================================================

TEST(CoreSink, BufferSinkZeroRemainingCapacity) {
  // Fill to exact capacity
  microfmt::buffer_sink<5> buf;
  microfmt::format_to(buf.as_sink(), "12345");
  EXPECT_EQ(buf.size(), 5u);
  EXPECT_EQ(buf.view(), "12345");

  // Subsequent writes to a full buffer should be completely ignored
  microfmt::format_to(buf.as_sink(), "67890");
  EXPECT_EQ(buf.size(), 5u);
  EXPECT_EQ(buf.view(), "12345");
}

TEST(CoreSink, SpanSinkExactBoundaryWrites) {
  char target[4];
  microfmt::span_sink ss(microfmt::span<char>(target, 4));

  // Write 2 chars, then 2 chars
  microfmt::format_to(ss.as_sink(), "AB");
  EXPECT_EQ(ss.size(), 2u);
  EXPECT_EQ(ss.view(), "AB");

  microfmt::format_to(ss.as_sink(), "CD");
  EXPECT_EQ(ss.size(), 4u);
  EXPECT_EQ(ss.view(), "ABCD");

  // Next write overflows
  microfmt::format_to(ss.as_sink(), "E");
  EXPECT_EQ(ss.size(), 4u);
  EXPECT_EQ(ss.view(), "ABCD");
}

TEST(CoreSink, PutSingleChar) {
  microfmt::buffer_sink<8> buf;
  auto s = buf.as_sink();
  s.put('A');
  s.put('B');
  s.put('C');
  EXPECT_EQ(buf.view(), "ABC");
  EXPECT_EQ(buf.size(), 3u);
}

// ============================================================================
// Advanced Custom Formatter with Specifier Parsing
// ============================================================================

struct RegisterDump {
  uint32_t val;
};

template <> struct microfmt::formatter<RegisterDump> {
  char fmt_mode{'x'}; // 'x' = hex, 'b' = binary, 'd' = decimal

  constexpr void parse(format_parse_context &ctx) noexcept {
    if (!ctx.empty()) {
      fmt_mode = ctx.front();
    }
  }

  void format(const RegisterDump &reg, const sink &out) const noexcept {
    if (fmt_mode == 'b') {
      out.write("0b");
      detail::format_unsigned(out, reg.val, 2, false, 8);
    } else if (fmt_mode == 'd') {
      detail::format_unsigned(out, reg.val, 10, false, 0);
    } else {
      out.write("0x");
      detail::format_unsigned(out, reg.val, 16, false, 8);
    }
  }
};

TEST(CoreCustomType, SpecifierDrivenFormatting) {
  RegisterDump reg{0b00101011}; // 43 or 0x2b

  EXPECT_EQ(microfmt::format<32>("{:x}", reg).view(), "0x0000002b");
  EXPECT_EQ(microfmt::format<32>("{:b}", reg).view(), "0b00101011");
  EXPECT_EQ(microfmt::format<32>("{:d}", reg).view(), "43");
}

TEST(CoreCustomType, NestedFormattingInsideCustomFormatter) {
  struct Inner {
    int val;
  };
  struct Outer {
    Inner a;
    Inner b;
  };

  // Lambda or struct-level specialization test
  // Both inner and outer format using microfmt recursively
  auto r = microfmt::format<32>("[{}, {}]", 10, 20);
  EXPECT_EQ(r.view(), "[10, 20]");
}