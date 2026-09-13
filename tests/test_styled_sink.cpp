#include <gtest/gtest.h>

#include <microfmt/microfmt.hpp>
#include <microfmt/sinks/styled_sink.hpp>

TEST(StyledSinkTest, TransformsAsciiAndPreservesWhitespace) {
  microfmt::buffer_sink<64> output;
  microfmt::transform_sink transformed(
      output.as_sink(), microfmt::char_transform::to_upper);

  microfmt::format_to(transformed.as_sink(), "state={}\n", "ready");
  EXPECT_EQ(output.view(), "STATE=READY\n");

  transformed.set_transform(microfmt::char_transform::to_lower);
  microfmt::format_to(transformed.as_sink(), "VCC={}", "OK");
  EXPECT_EQ(output.view(), "STATE=READY\nvcc=ok");
}

TEST(StyledSinkTest, SanitizesAsciiControlCharacters) {
  microfmt::buffer_sink<64> output;
  microfmt::transform_sink sanitized(
      output.as_sink(), microfmt::char_transform::sanitize_ascii);
  const char input[] = {'A', '\x01', 'B', '\n', 'C', '\t',
                        'D', '\r',   'E', '\x7f', 'F'};

  sanitized.write(std::string_view(input, sizeof(input)));

  EXPECT_EQ(output.view(), "A.B\nC\tD\rE.F");
}

TEST(StyledSinkTest, PrefixesLinesAcrossSeparateWrites) {
  microfmt::buffer_sink<128> output;
  microfmt::prefix_sink prefixed(output.as_sink(), "[trace] ");

  microfmt::format_to(prefixed.as_sink(), "boot={}\n", "ready");
  prefixed.write("sensor=42");
  prefixed.write("\n");
  prefixed.set_prefix("[warn] ");
  prefixed.write("voltage=3080");

  EXPECT_EQ(output.view(),
            "[trace] boot=ready\n[trace] sensor=42\n[warn] voltage=3080");
}

TEST(StyledSinkTest, LimitsOutputAtConfiguredByteCount) {
  microfmt::buffer_sink<64> output;
  microfmt::limit_sink limited(output.as_sink(), 10);

  microfmt::format_to(limited.as_sink(), "id={} state={}", 42, "ok");
  limited.write(" ignored");

  EXPECT_EQ(output.view(), "id=42 stat");
  EXPECT_EQ(limited.remaining(), 0U);
  EXPECT_TRUE(limited.capped());
}

TEST(StyledSinkTest, ZeroLimitDiscardsAllOutput) {
  microfmt::buffer_sink<16> output;
  microfmt::limit_sink limited(output.as_sink(), 0);

  limited.put('x');
  limited.write("ignored");

  EXPECT_TRUE(output.view().empty());
  EXPECT_TRUE(limited.capped());
}
