// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <microfmt/formatters/bintime.hpp>
#include <microfmt/formatters/floating.hpp>
#include <microfmt/microfmt.hpp>

#include <cctype>
#include <charconv>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>

TEST(CoreBintime, FormatsPicosecondPrecisionWithoutWideIntegers) {
  struct binary_time {
    int64_t sec;
    uint64_t frac;
  };

  EXPECT_EQ(microfmt::format<32>("{:p}", binary_time{1, UINT64_C(0x8000000000000000)}).view(), "1.500000000000s");
  EXPECT_EQ(microfmt::format<32>("{:p}", binary_time{1, UINT64_MAX}).view(), "1.999999999999s");
}

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

  microfmt::sink custom_sink{&ctx, [](void *context, microfmt::string_view sv) noexcept {
                               auto *c = static_cast<TestContext *>(context);
                               (*c->calls)++;
                               *c->bytes += sv.size();
                             }};

  microfmt::format_to(custom_sink, "A = {}, B = {}", 1, "test");
  EXPECT_GT(write_call_count, 0u);
  EXPECT_EQ(total_bytes, 15u); // "A = 1, B = test"
}

TEST(CoreSink, IgnoresEmptyWritesAndMissingCallbacks) {
  size_t calls = 0;
  microfmt::sink empty_sink{};
  empty_sink.write("ignored");
  empty_sink.put('x');

  microfmt::sink observed_sink{&calls,
                               [](void *ctx, microfmt::string_view) noexcept { ++*static_cast<size_t *>(ctx); }};
  observed_sink.write({});
  EXPECT_EQ(calls, 0U);
  observed_sink.write("x");
  EXPECT_EQ(calls, 1U);
}

TEST(CoreSink, SpanSinkAccessorsAndReset) {
  char storage[5] = {};
  microfmt::span_sink output{microfmt::span<char>(storage)};
  EXPECT_EQ(output.size(), 0U);
  EXPECT_EQ(output.available(), 5U);

  microfmt::format_to(output.as_sink(), "abc");
  EXPECT_EQ(output.view(), "abc");
  EXPECT_EQ(output.available(), 2U);

  output.reset();
  EXPECT_TRUE(output.view().empty());
  EXPECT_EQ(output.available(), 5U);

#if RELOCO_HAS_STD_SPAN
  microfmt::span_sink standard_output{std::span<char>(storage)};
  microfmt::format_to(standard_output.as_sink(), "xy");
  EXPECT_EQ(standard_output.view(), "xy");
#endif
}

TEST(CoreSink, BufferSinkAccessors) {
  microfmt::buffer_sink<4> output;
  microfmt::format_to(output.as_sink(), "abcde");

  EXPECT_EQ(output.size(), 4U);
  EXPECT_EQ(output.capacity(), 4U);
  EXPECT_EQ(output.available(), 0U);
  const auto output_span = output.as_span();
  EXPECT_EQ(output_span.size(), 4U);
  EXPECT_EQ(microfmt::string_view(output_span.data(), output_span.size()), "abcd");
#if RELOCO_HAS_STD_SPAN
  const auto standard_span = output.as_std_span();
  EXPECT_EQ(standard_span.size(), 4U);
  EXPECT_EQ(standard_span.data(), output_span.data());
#endif
}

TEST(CoreSink, CountingNullAndCallbackAdapters) {
  microfmt::counting_sink counter;
  microfmt::format_to(counter.as_sink(), "{}-{}", "abc", 42);
  EXPECT_EQ(counter.count(), 6U);
  counter.reset();
  EXPECT_EQ(counter.count(), 0U);

  microfmt::format_to(microfmt::null_sink::as_sink(), "discard {} {}", 1, 2);

  std::string captured;
  auto append = [&captured](microfmt::string_view text) { captured.append(text.data(), text.size()); };
  auto callback = microfmt::make_callback_sink(append);
  microfmt::format_to(callback.as_sink(), "{}:{}", "value", 7);
  EXPECT_EQ(captured, "value:7");
}

TEST(CoreSink, CStringSinkTerminatesTruncatesAndResets) {
  microfmt::c_string_sink<6> output;
  EXPECT_STREQ(output.c_str(), "");
  EXPECT_EQ(output.max_size(), 5U);

  microfmt::format_to(output.as_sink(), "abcdef");
  EXPECT_EQ(output.view(), "abcde");
  EXPECT_EQ(output.size(), 5U);
  EXPECT_STREQ(output.c_str(), "abcde");

  output.reset();
  EXPECT_TRUE(output.view().empty());
  EXPECT_STREQ(output.c_str(), "");

  microfmt::c_string_sink<1> terminator_only;
  microfmt::format_to(terminator_only.as_sink(), "ignored");
  EXPECT_EQ(terminator_only.size(), 0U);
  EXPECT_STREQ(terminator_only.c_str(), "");
}

TEST(CoreSink, IteratorSinkSupportsStandardOutputIterators) {
  std::string output;
  auto end = microfmt::format_to(std::back_inserter(output), "{}-{}", 12, "ok");
  *end = '!';
  EXPECT_EQ(output, "12-ok!");

  output.clear();
  microfmt::format_to(std::back_inserter(output), MICROFMT_STRING("{1}:{0}"), "left", "right");
  EXPECT_EQ(output, "right:left");
}

// ============================================================================
// Formatter Core: Primitives, Radix, Specifiers & Escaping
// ============================================================================

TEST(CoreFormat, ParseContextOperations) {
  microfmt::format_parse_context context("abc:def");
  EXPECT_FALSE(context.empty());
  EXPECT_EQ(context.size(), 7U);
  EXPECT_EQ(context.front(), 'a');
  EXPECT_EQ(context.back(), 'f');
  EXPECT_EQ(context[2], 'c');
  EXPECT_EQ(context[99], '\0');
  EXPECT_TRUE(context.starts_with('a'));
  EXPECT_TRUE(context.starts_with("abc"));
  EXPECT_EQ(context.find(':'), 3U);
  EXPECT_EQ(context.substr(4), "def");

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  context.advance_to(context.begin() + 2);

  RELOCO_END_UNSAFE_BUFFER_USAGE;

  EXPECT_EQ(context.spec(), "c:def");
  EXPECT_EQ(context.consume(), 'c');
  context.remove_prefix(100);
  EXPECT_TRUE(context.empty());
  EXPECT_EQ(context.front(), '\0');
  EXPECT_EQ(context.back(), '\0');
  EXPECT_EQ(context.consume(), '\0');
}

TEST(CoreDetail, IntegerHelpersCoverSignsWidthsAndRadices) {
  microfmt::buffer_sink<64> output;

  microfmt::detail::format_signed(output.as_sink(), -42);
  EXPECT_EQ(output.view(), "-42");

  output.reset();
  microfmt::detail::format_signed(output.as_sink(), -42, 5);
  EXPECT_EQ(output.view(), "-0042");

  output.reset();
  microfmt::detail::format_signed(output.as_sink(), 42);
  EXPECT_EQ(output.view(), "42");

  output.reset();
  microfmt::detail::format_unsigned<microfmt::detail::radix::binary>(output.as_sink(), 0, false);
  EXPECT_EQ(output.view(), "0");

  output.reset();
  microfmt::detail::format_unsigned<microfmt::detail::radix::hex>(output.as_sink(), 0xAB, true);
  EXPECT_EQ(output.view(), "AB");

  output.reset();
  microfmt::detail::format_unsigned<microfmt::detail::radix::binary>(output.as_sink(), 0b101101, false, 8);
  EXPECT_EQ(output.view(), "00101101");
}

namespace {

// Cross-checks microfmt::detail::format_unsigned's output against
// std::to_chars (the standard library's own radix-aware integer formatter)
// for a single value/radix/case combination. Comparing against an
// independent, standard implementation (rather than hand-rolling an
// expected string) avoids re-introducing the same casting mistakes the
// code under test might have.
template <typename T> void check_format_unsigned(T value, int radix, bool uppercase) {
  static_assert(std::is_unsigned_v<T>, "check_format_unsigned expects an unsigned type");

  microfmt::buffer_sink<80> output;
  // format_unsigned's radix is now a compile-time template parameter (see
  // microfmt::detail::radix), so dispatch the runtime `radix` value under
  // test to the matching instantiation.
  switch (radix) {
  case 2:
    microfmt::detail::format_unsigned<microfmt::detail::radix::binary>(output.as_sink(), static_cast<uint64_t>(value),
                                                                        uppercase);
    break;
  case 10:
    microfmt::detail::format_unsigned<microfmt::detail::radix::decimal>(
        output.as_sink(), static_cast<uint64_t>(value), uppercase);
    break;
  case 16:
    microfmt::detail::format_unsigned<microfmt::detail::radix::hex>(output.as_sink(), static_cast<uint64_t>(value),
                                                                     uppercase);
    break;
  default:
    FAIL() << "unsupported radix under test: " << radix;
    return;
  }

  char ref_buf[80];
  auto ref_res = std::to_chars(ref_buf, ref_buf + sizeof(ref_buf), value, radix);
  ASSERT_EQ(ref_res.ec, std::errc());
  std::string expected(ref_buf, static_cast<size_t>(ref_res.ptr - ref_buf));
  if (uppercase) {
    for (char &c : expected)
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }

  EXPECT_EQ(output.view(), microfmt::string_view(expected)) << "T=" << typeid(T).name() << " value=" << +value
                                                             << " radix=" << radix << " uppercase=" << uppercase;
}

// Same idea for microfmt::detail::format_signed, which only ever formats in
// base 10 (there is no radix parameter): std::to_chars(..., 10) already
// produces the same "-123"-style representation for negative values.
template <typename T> void check_format_signed(T value) {
  static_assert(std::is_signed_v<T>, "check_format_signed expects a signed type");

  microfmt::buffer_sink<80> output;
  microfmt::detail::format_signed(output.as_sink(), static_cast<int64_t>(value));

  char ref_buf[80];
  auto ref_res = std::to_chars(ref_buf, ref_buf + sizeof(ref_buf), value, 10);
  ASSERT_EQ(ref_res.ec, std::errc());
  std::string_view expected(ref_buf, static_cast<size_t>(ref_res.ptr - ref_buf));

  EXPECT_EQ(output.view(), microfmt::string_view(expected)) << "T=" << typeid(T).name() << " value=" << +value;
}

template <typename T> void check_unsigned_type_all_radixes() {
  const T boundary_values[] = {T(0), T(1), T(2), std::numeric_limits<T>::max(),
                               static_cast<T>(std::numeric_limits<T>::max() / 2)};
  for (T value : boundary_values) {
    for (int radix : {2, 10, 16}) {
      check_format_unsigned(value, radix, false);
      check_format_unsigned(value, radix, true);
    }
  }
}

template <typename T> void check_signed_type_all_radixes() {
  // format_signed only supports base 10 -- there is no radix parameter --
  // so decimal is the only "supported radix" for the signed core helper.
  const T boundary_values[] = {std::numeric_limits<T>::min(), T(-1), T(0), T(1), std::numeric_limits<T>::max()};
  for (T value : boundary_values)
    check_format_signed(value);
}

} // namespace

TEST(CoreDetail, FormatUnsignedCoversAllIntegerTypesAndRadixes) {
  check_unsigned_type_all_radixes<uint8_t>();
  check_unsigned_type_all_radixes<uint16_t>();
  check_unsigned_type_all_radixes<uint32_t>();
  check_unsigned_type_all_radixes<uint64_t>();
}

TEST(CoreDetail, FormatSignedCoversAllIntegerTypesAndRadixes) {
  check_signed_type_all_radixes<int8_t>();
  check_signed_type_all_radixes<int16_t>();
  check_signed_type_all_radixes<int32_t>();
  check_signed_type_all_radixes<int64_t>();
}

TEST(CoreDetail, ParserHelpersHandlePrefixesAndInvalidPositions) {
  EXPECT_TRUE(microfmt::detail::starts_with("prefix", "pre"));
  EXPECT_FALSE(microfmt::detail::starts_with("pre", "prefix"));
  EXPECT_TRUE(microfmt::detail::starts_with("prefix", 'p'));
  EXPECT_FALSE(microfmt::detail::starts_with("", 'p'));

  size_t index = 7;
  EXPECT_FALSE(microfmt::detail::parse_positional_index("", index));
  EXPECT_EQ(index, 7U);
  EXPECT_FALSE(microfmt::detail::parse_positional_index("name", index));
  EXPECT_EQ(index, 7U);
  EXPECT_TRUE(microfmt::detail::parse_positional_index("0012", index));
  EXPECT_EQ(index, 12U);
}

TEST(CoreFormat, IntegerRadixAndWidth) {
  // Decimal (signed and unsigned)
  EXPECT_EQ(microfmt::format<32>("{}", 0).view(), "0");
  EXPECT_EQ(microfmt::format<32>("{}", -1).view(), "-1");
  EXPECT_EQ(microfmt::format<32>("{}", 2147483647).view(), "2147483647");
  EXPECT_EQ(microfmt::format<32>("{}", -2147483648LL).view(), "-2147483648");
  EXPECT_EQ(microfmt::format<32>("{}", 18446744073709551615ULL).view(), "18446744073709551615");

  // Hex lowercase and uppercase
  EXPECT_EQ(microfmt::format<32>("{:x}", 0xbeef).view(), "beef");
  EXPECT_EQ(microfmt::format<32>("{:X}", 0xbeef).view(), "BEEF");

  // Hex with zero padding
  EXPECT_EQ(microfmt::format<32>("{:04x}", 0x1a).view(), "001a");
  EXPECT_EQ(microfmt::format<32>("{:08X}", 0x1a2b).view(), "00001A2B");
  EXPECT_EQ(microfmt::format<32>("{:016x}", 0xffff800000000000ULL).view(), "ffff800000000000");
}

TEST(CoreFormat, IntegerFlagsAndPaddingCombinations) {
  EXPECT_EQ(microfmt::format<16>("{:#x}", 0x2A).view(), "0x2a");
  EXPECT_EQ(microfmt::format<16>("{:#X}", 0x2A).view(), "0X2A");
  EXPECT_EQ(microfmt::format<16>("{:#08x}", 0x2A).view(), "0x00002a");
  EXPECT_EQ(microfmt::format<16>("{:6}", 42).view(), "    42");
  EXPECT_EQ(microfmt::format<16>("{:6}", -42).view(), "   -42");
  EXPECT_EQ(microfmt::format<16>("{:06}", -42).view(), "-00042");
  EXPECT_EQ(microfmt::format<16>("{:#08x}", -42).view(), "-0x0002a");
}

TEST(CoreFormat, CharAndBoolOutput) {
  EXPECT_EQ(microfmt::format<16>("{}", 'Z').view(), "Z");
  EXPECT_EQ(microfmt::format<16>("{} / {}", true, false).view(), "true / false");
}

TEST(CoreFormat, StringAndNullStringHandling) {
  const char *valid_str = "embedded";
  const char *null_str = nullptr;
  microfmt::string_view sv = "system";

  EXPECT_EQ(microfmt::format<32>("{} {}", valid_str, sv).view(), "embedded system");
  EXPECT_EQ(microfmt::format<32>("ptr: {}", null_str).view(), "ptr: (null)");

  std::string_view standard = "standard";
  char mutable_text[] = "mutable";
  char *mutable_ptr = mutable_text;
  char *null_mutable_ptr = nullptr;
  EXPECT_EQ(microfmt::format<32>("{} {}", standard, mutable_ptr).view(), "standard mutable");
  EXPECT_EQ(microfmt::format<32>("{}", null_mutable_ptr).view(), "(null)");
}

TEST(CoreFormat, PointerFormatting) {
  void *null_p = nullptr;
  EXPECT_EQ(microfmt::format<32>("{}", null_p).view(), "0x0");

  const void *addr = reinterpret_cast<const void *>(0x1000);
  EXPECT_EQ(microfmt::format<32>("{}", addr).view(), "0x1000");
  EXPECT_EQ(microfmt::format<32>("{:08}", addr).view(), "0x00001000");
  EXPECT_EQ(microfmt::format<16>("{}", nullptr).view(), "0x0");
}

TEST(CoreFormat, EscapeBraceSequences) {
  EXPECT_EQ(microfmt::format<32>("{{").view(), "{");
  EXPECT_EQ(microfmt::format<32>("}}").view(), "}");
  EXPECT_EQ(microfmt::format<32>("{{{}}}", 42).view(), "{42}");
  EXPECT_EQ(microfmt::format<32>("{{noparam}}").view(), "{noparam}");
}

TEST(CoreFormat, ArgumentMismatchSafety) {
  // Missing arguments
  EXPECT_EQ(microfmt::format<32>("A: {}, B: {}", 1).view(), "A: 1, B: {MISSING}");

  // Extra unused arguments should format without crashing
  EXPECT_EQ(microfmt::format<32>("Only: {}", 10, 20, 30).view(), "Only: 10");

  // Empty format string with arguments
  EXPECT_EQ(microfmt::format<32>("", 10, 20).view(), "");
}

TEST(CoreFormat, MalformedRuntimeFieldsRemainLiteral) {
  EXPECT_EQ(microfmt::format<32>("before { after", 42).view(), "before { after");
  EXPECT_EQ(microfmt::format<32>("single } brace").view(), "single } brace");
  EXPECT_EQ(microfmt::format<32>("{} trailing {", 42).view(), "42 trailing {");
}

TEST(CoreFormat, NumericPositionalArguments) {
  EXPECT_EQ(microfmt::format<64>("{2} {0} {1:04x} {2}", "first", 0x2A, "last").view(), "last first 002a last");
  EXPECT_EQ(microfmt::format<32>("{1} {}", "first", "second").view(), "second first");
  EXPECT_EQ(microfmt::format<32>("{3}", "first", "second").view(), "{MISSING}");
}

TEST(CoreFormat, NumericPositionalArgumentEdges) {
  EXPECT_EQ(microfmt::format<64>("{10} {00} {01:04x}", "zero", 0x2A, 2, 3, 4, 5, 6, 7, 8, 9, "ten").view(),
            "ten zero 002a");
  EXPECT_EQ(microfmt::format<32>("{9} {}", "automatic").view(), "{MISSING} automatic");
  EXPECT_EQ(microfmt::format<32>("{{{1}}}", "first", "second").view(), "{second}");
  EXPECT_EQ(microfmt::format<32>("{0}").view(), "{MISSING}");
  EXPECT_EQ(microfmt::format<32>("{184467440737095516161234}", 42).view(), "{MISSING}");
}

TEST(CoreFormat, CompileTimeNumericPositionalArguments) {
  EXPECT_EQ(microfmt::format<64>(MICROFMT_STRING("{2} {0} {1:04x} {2}"), "first", 0x2A, "last").view(),
            "last first 002a last");
  EXPECT_EQ(microfmt::format<32>(MICROFMT_STRING("{1} {}"), "first", "second").view(), "second first");
  EXPECT_EQ(microfmt::format<32>(MICROFMT_STRING("{{{1}}}"), "first", "second").view(), "{second}");
}

TEST(CoreFormat, NumericPositionalRuntimeStress) {
  std::string format_string;
  format_string.reserve(256 * 3);
  for (size_t i = 0; i < 256; ++i) {
    format_string += "{0}";
  }

  const auto result = microfmt::format<256>(format_string, 'x');
  const std::string expected(256, 'x');
  EXPECT_EQ(result.size(), 256U);
  EXPECT_EQ(std::string_view(result.view()), expected);
}

TEST(CoreFormat, NumericPositionalCompileTimeStress) {
  const auto result = microfmt::format<64>(MICROFMT_STRING("{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}"
                                                           "{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}"
                                                           "{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}"
                                                           "{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}{0}"),
                                           'x');
  EXPECT_EQ(result.view(), "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
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

  void format(const Point2D &p, const sink &out) const noexcept { microfmt::format_to(out, "Point({}, {})", p.x, p.y); }
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
  EXPECT_EQ(microfmt::format<32>("0x{:x}", max_u64).view(), "0xffffffffffffffff");
  EXPECT_EQ(microfmt::format<32>("0x{:X}", max_u64).view(), "0xFFFFFFFFFFFFFFFF");
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
  EXPECT_EQ(microfmt::format<32>("Literal: {}", "const literal").view(), "Literal: const literal");

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
  EXPECT_EQ(microfmt::format<32>("{:02x}{:02x}{:02x}", 0x1, 0xA, 0xFF).view(), "010aff");

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
      detail::format_binary(out, reg.val, 8);
    } else if (fmt_mode == 'd') {
      detail::format_unsigned<detail::radix::decimal>(out, reg.val, false, 0);
    } else {
      out.write("0x");
      detail::format_unsigned<detail::radix::hex>(out, reg.val, false, 8);
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

TEST(FormatAsTest, RuntimeFormatStdString) {
  auto result = microfmt::format_as<std::string>("Hello {} and {}", "World", 42);
  EXPECT_EQ(result, "Hello World and 42");
}

TEST(FormatAsTest, CompileTimeFormatStdString) {
  auto result = microfmt::format_as<std::string>(MICROFMT_STRING("Value: {}"), 123);
  EXPECT_EQ(result, "Value: 123");
}

TEST(FormatAsTest, RuntimeFormatVectorChar) {
  auto result = microfmt::format_as<std::vector<char>>("Status code: {}", 200);

  std::string_view sv(result.data(), result.size());
  EXPECT_EQ(sv, "Status code: 200");
}

TEST(FormatAsTest, CompileTimeFormatVectorChar) {
  auto result = microfmt::format_as<std::vector<char>>(MICROFMT_STRING("Hex: {:x}"), 255);

  std::string_view sv(result.data(), result.size());
  EXPECT_EQ(sv, "Hex: ff"); // Assuming standard fmt-like syntax for hex
}

TEST(FormatAsTest, EmptyFormatString) {
  auto runtime_result = microfmt::format_as<std::string>("");
  EXPECT_TRUE(runtime_result.empty());

  auto compile_result = microfmt::format_as<std::string>(MICROFMT_STRING(""));
  EXPECT_TRUE(compile_result.empty());
}

TEST(FormatAsTest, NoArguments) {
  auto result = microfmt::format_as<std::string>("Just text");
  EXPECT_EQ(result, "Just text");

  auto compile_result = microfmt::format_as<std::string>(MICROFMT_STRING("Compile text"));
  EXPECT_EQ(compile_result, "Compile text");
}

// Helper to simulate an ABI boundary or a non-templated logger
static std::string dispatch_to_logger(microfmt::string_view fmt, microfmt::format_args args) {
  // Can be passed around cheaply, and formatted locally
  return microfmt::vformat_args_as<std::string>(fmt, args);
}

TEST(FormatArgsTest, ZeroArguments) {
  auto args_store = microfmt::make_format_args();
  microfmt::format_args view = args_store;

  EXPECT_TRUE(view.ptrs.empty());
  EXPECT_TRUE(view.fns.empty());

  std::string result = vformat_args_as<std::string>("No arguments here", view);
  EXPECT_EQ(result, "No arguments here");
}

TEST(FormatArgsTest, MultipleArguments) {
  int code = 404;
  std::string_view msg = "Not Found";
  auto args_store = microfmt::make_format_args(code, msg);

  microfmt::format_args view = args_store;

  EXPECT_EQ(view.ptrs.size(), 2);
  EXPECT_EQ(view.fns.size(), 2);

  std::string result = microfmt::vformat_args_as<std::string>("Error {}: {}", view);
  EXPECT_EQ(result, "Error 404: Not Found");
}

TEST(FormatArgsTest, TypeErasedLambdaCapture) {
  int a = 10, b = 20;
  auto args_store = microfmt::make_format_args(a, b);

  // Implicit conversion to format_args view
  microfmt::format_args view = args_store;

  auto deferred_formatter = [fmt = "{} + {} = 30", view]() {
    return microfmt::vformat_args_as<std::string>(fmt, view);
  };

  EXPECT_EQ(deferred_formatter(), "10 + 20 = 30");
}

TEST(FormatArgsTest, CustomContainerFormatAs) {
  // Use inline make_format_args. The temporaries (101010) live until
  // vformat_args_as completely finishes.
  auto result =
      microfmt::vformat_args_as<std::vector<char>>("Data: {} {}", microfmt::make_format_args("binary", 101010));

  std::string_view sv(result.data(), result.size());
  EXPECT_EQ(sv, "Data: binary 101010");
}

TEST(FormatArgsTest, VFormatArgsToSpanSink) {
  char buffer[64];
  microfmt::span_sink s_sink(buffer);

  // Note: Argument order swapped to match the expected string output
  microfmt::vformat_args_to(s_sink.as_sink(), "{} is approximately {}", microfmt::make_format_args("Pi", 3.14));

  EXPECT_EQ(s_sink.view(), "Pi is approximately 3.14");
}

TEST(FormatArgsTest, NonTemplatedBoundary) {
  // Verifies that a non-templated function can successfully accept
  // the type-erased args and format them dynamically.
  std::string log_msg = dispatch_to_logger("System {} is {}", microfmt::make_format_args("Engine", "Online"));

  EXPECT_EQ(log_msg, "System Engine is Online");
}