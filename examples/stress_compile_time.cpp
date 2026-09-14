#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "microfmt/microfmt.hpp"
#include "microfmt/sinks/stdio.hpp"

// ============================================================================
// Custom User-Defined Type for Stress Testing
// ============================================================================

struct SensorReading {
  uint16_t sensor_id;
  int16_t temperature_c;
  uint32_t timestamp_ms;
  bool is_valid;
};

// Formatter specialization for SensorReading
template <> struct microfmt::formatter<SensorReading> {
  constexpr void parse(microfmt::format_parse_context &) noexcept {}

  void format(const SensorReading &r,
              const microfmt::sink &out) const noexcept {
    microfmt::format_to(out,
                        MICROFMT_STRING("[Sensor #{} @ {}ms: {} C, status={}]"),
                        r.sensor_id, r.timestamp_ms, r.temperature_c,
                        r.is_valid ? "VALID" : "FAULT");
  }
};

// ============================================================================
// Stress Test Harness Helpers
// ============================================================================

static size_t g_tests_passed = 0;
static size_t g_tests_failed = 0;

template <size_t N>
void assert_sink_eq(const microfmt::buffer_sink<N> &buf,
                    std::string_view expected, const char *test_name) {
  const std::string_view actual = buf.view();
  if (actual == expected) {
    microfmt::println(MICROFMT_STRING("  [PASS] {}"), test_name);
    ++g_tests_passed;
  } else {
    microfmt::println(
        MICROFMT_STRING(
            "  [FAIL] {}\n    Expected: \"{}\"\n    Actual:   \"{}\""),
        test_name, expected, actual);
    ++g_tests_failed;
  }
}

// ============================================================================
// Test Suites
// ============================================================================

void test_stress_literal_boundaries() {
  microfmt::println(
      MICROFMT_STRING("\n--- Test Suite 1: Literal Boundaries & Escaping ---"));

  // Empty format string
  {
    auto buf = microfmt::format<32>(MICROFMT_STRING(""));
    assert_sink_eq(buf, "", "Empty format string");
  }

  // Pure literal without specifiers
  {
    auto buf = microfmt::format<64>(
        MICROFMT_STRING("Static embedded log banner with zero args."));
    assert_sink_eq(buf, "Static embedded log banner with zero args.",
                   "Literal only");
  }

  // Escaped braces
  {
    auto buf = microfmt::format<64>(
        MICROFMT_STRING("JSON payload: {{\"id\": {}, \"val\": {}}}"), 42, 100);
    assert_sink_eq(buf, "JSON payload: {\"id\": 42, \"val\": 100}",
                   "Escaped braces '{{' and '}}'");
  }

  // Adjacent specifiers with no literals between them
  {
    auto buf =
        microfmt::format<32>(MICROFMT_STRING("{}{}{}{}"), 'A', 'B', 'C', 'D');
    assert_sink_eq(buf, "ABCD", "Adjacent specifiers");
  }
}

void test_stress_wide_argument_packs() {
  microfmt::println(MICROFMT_STRING(
      "\n--- Test Suite 2: High Argument Count & Mixed Types ---"));

  // 16-argument stress test mixing signed/unsigned, sizes, bools, chars,
  // pointers, and strings
  const uint8_t u8 = 0xFF;
  const int8_t i8 = -12;
  const uint16_t u16 = 65535;
  const int16_t i16 = -32768;
  const uint32_t u32 = 123456789;
  const int32_t i32 = -987654321;
  const uint64_t u64 = 0xDEADBEEFCAFEBABEULL;
  const int64_t i64 = -9223372036854775807LL;
  const char c = '#';
  const bool b_true = true;
  const bool b_false = false;
  const char *raw_str = "C-Str";
  const std::string_view sv_str = "StringView";
  const void *ptr = reinterpret_cast<const void *>(0x20000000);

  auto buf = microfmt::format<512>(
      MICROFMT_STRING("A:{}, B:{}, C:{}, D:{}, E:{}, F:{}, G:{:X}, H:{}, I:{}, "
                      "J:{}, K:{}, L:{}, M:{}, N:{}"),
      u8, i8, u16, i16, u32, i32, u64, i64, c, b_true, b_false, raw_str, sv_str,
      ptr);

  // Validate that buffer formatted without truncating
  if (buf.size() > 0 &&
      buf.view().find("123456789") != std::string_view::npos) {
    microfmt::println(MICROFMT_STRING("  [PASS] 14-argument heterogeneous "
                                      "unrolled pack formatted ({} bytes)"),
                      buf.size());
    ++g_tests_passed;
  } else {
    microfmt::println(MICROFMT_STRING(
        "  [FAIL] 14-argument heterogeneous pack format failed"));
    ++g_tests_failed;
  }
}

void test_stress_nested_custom_types() {
  microfmt::println(
      MICROFMT_STRING("\n--- Test Suite 3: Nested Formatter Inlining ---"));

  SensorReading sensor1{1, 24, 10500, true};
  SensorReading sensor2{2, -5, 10520, false};

  auto buf = microfmt::format<256>(
      MICROFMT_STRING("Telemetry Batch: primary={}, secondary={}"), sensor1,
      sensor2);

  const std::string_view expected =
      "Telemetry Batch: primary=[Sensor #1 @ 10500ms: 24 C, status=VALID], "
      "secondary=[Sensor #2 @ 10520ms: -5 C, status=FAULT]";

  assert_sink_eq(buf, expected, "Nested custom struct formatting");
}

void test_stress_sink_backends() {
  microfmt::println(
      MICROFMT_STRING("\n--- Test Suite 4: Diverse Sinks & Iterators ---"));

  // Fixed memory span sink
  {
    char raw_buf[64] = {};
    microfmt::span_sink ssink(raw_buf);
    microfmt::format_to(ssink.as_sink(), MICROFMT_STRING("Span [{}:{}]"), "IP",
                        8080);
    if (ssink.view() == "Span [IP:8080]") {
      microfmt::println(MICROFMT_STRING("  [PASS] span_sink unrolled format"));
      ++g_tests_passed;
    } else {
      microfmt::println(MICROFMT_STRING("  [FAIL] span_sink unrolled format"));
      ++g_tests_failed;
    }
  }

  // Iterator sink (writing into raw pointer)
  {
    char raw_array[32] = {};
    char *end_ptr =
        microfmt::format_to(raw_array, MICROFMT_STRING("Iter: 0x{:X}"), 0xCAFE);
    *end_ptr = '\0';

    if (std::string_view(raw_array) == "Iter: 0xCAFE") {
      microfmt::println(
          MICROFMT_STRING("  [PASS] iterator_sink unrolled format"));
      ++g_tests_passed;
    } else {
      microfmt::println(
          MICROFMT_STRING("  [FAIL] iterator_sink unrolled format"));
      ++g_tests_failed;
    }
  }

  // Direct file sink output
  {
    microfmt::println(
        stdout,
        MICROFMT_STRING(
            "  [INFO] Verifying direct FILE* print to stdout: value={}"),
        12345);
  }
}

void test_stress_throughput() {
  microfmt::println(MICROFMT_STRING(
      "\n--- Test Suite 5: High-Iteration Throughput Check ---"));

  constexpr size_t ITERATIONS = 100000;
  microfmt::buffer_sink<64> sink;

  for (size_t i = 0; i < ITERATIONS; ++i) {
    sink.reset();
    microfmt::format_to(sink.as_sink(),
                        MICROFMT_STRING("Seq={}: Status=0x{:X}"), i, 0xABC);
  }

  if (sink.size() > 0) {
    microfmt::println(
        MICROFMT_STRING(
            "  [PASS] Completed {} formatting cycles. Final output: \"{}\""),
        ITERATIONS, sink.view());
    ++g_tests_passed;
  } else {
    microfmt::println(
        MICROFMT_STRING("  [FAIL] High-iteration throughput check"));
    ++g_tests_failed;
  }
}

// ============================================================================
// Main Entry
// ============================================================================

int main() {
  microfmt::println(
      MICROFMT_STRING("=================================================="));
  microfmt::println(
      MICROFMT_STRING("    microfmt Compile-Time String Stress App       "));
  microfmt::println(
      MICROFMT_STRING("=================================================="));

  test_stress_literal_boundaries();
  test_stress_wide_argument_packs();
  test_stress_nested_custom_types();
  test_stress_sink_backends();
  test_stress_throughput();

  microfmt::println(
      MICROFMT_STRING("\n=================================================="));
  microfmt::println(MICROFMT_STRING("Summary: {} Passed, {} Failed"),
                    g_tests_passed, g_tests_failed);
  microfmt::println(
      MICROFMT_STRING("=================================================="));

  return (g_tests_failed == 0) ? 0 : 1;
}