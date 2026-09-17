// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/formatters/ansi.hpp>
#include <microfmt/formatters/bintime.hpp>
#include <microfmt/formatters/escaped.hpp>
#include <microfmt/formatters/floating.hpp>
#include <microfmt/formatters/format_helpers.hpp>
#include <microfmt/formatters/grid_view.hpp>
#include <microfmt/formatters/hexdump.hpp>
#include <microfmt/formatters/net.hpp>
#include <microfmt/formatters/posix_time.hpp>
#include <microfmt/formatters/register.hpp>
#include <microfmt/formatters/string.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace {

struct timespec_like {
  int64_t tv_sec;
  int32_t tv_nsec;
};

struct timeval_like {
  int64_t tv_sec;
  int32_t tv_usec;
};

struct bintime_like {
  int64_t sec;
  uint64_t frac;
};

struct checked_reader_context {
  uintptr_t base;
  std::array<uint8_t, 4> bytes;
  size_t readable;
};

size_t checked_reader(void *ctx, uintptr_t address, uint8_t *destination,
                      size_t requested) noexcept {
  auto &reader = *static_cast<checked_reader_context *>(ctx);
  const size_t offset = static_cast<size_t>(address - reader.base);
  if (offset >= reader.readable) {
    return 0;
  }

  const size_t available = reader.readable - offset;
  const size_t count = requested < available ? requested : available;
  for (size_t i = 0; i < count; ++i) {
    destination[i] = reader.bytes[offset + i];
  }
  return count;
}

} // namespace

TEST(AnsiFormatter, EmitsStylesAttributesAndScopedValues) {
  const bool previous = microfmt::ansi::style::colors_enabled;
  microfmt::ansi::style::colors_enabled = true;

  const microfmt::ansi::style combined{
      microfmt::ansi::color::red,
      microfmt::ansi::color::blue,
      microfmt::ansi::attribute::bold | microfmt::ansi::attribute::dim |
          microfmt::ansi::attribute::italic |
          microfmt::ansi::attribute::underline |
          microfmt::ansi::attribute::blink |
          microfmt::ansi::attribute::reverse,
  };
  EXPECT_EQ(microfmt::format<64>("{}text{}", combined, microfmt::ansi::reset).view(),
            "\033[1;2;3;4;5;7;31;44mtext\033[0m");
  EXPECT_EQ(microfmt::format<64>("{}", microfmt::ansi::green(42)).view(),
            "\033[32m42\033[0m");

  microfmt::ansi::style::colors_enabled = false;
  EXPECT_EQ(microfmt::format<32>("{}plain{}", microfmt::ansi::fg_red,
                                microfmt::ansi::reset)
                .view(),
            "plain");
  EXPECT_EQ(microfmt::format<32>("{}", microfmt::ansi::blue("value")).view(),
            "value");

  microfmt::ansi::style::colors_enabled = previous;
}

TEST(FloatingFormatter, SupportsTypesSignsPrecisionAndAlternateForm) {
  EXPECT_EQ(microfmt::format<32>("{:.2f}", 1.25).view(), "1.25");
  EXPECT_EQ(microfmt::format<32>("{:+.1f}", 2.0F).view(), "+2.0");
  EXPECT_EQ(microfmt::format<32>("{:#.0f}", 2.0).view(), "2.");
  EXPECT_EQ(microfmt::format<32>("{:.2e}", 12.0).view(), "1.20e+01");
  EXPECT_EQ(microfmt::format<32>("{:.1E}", 12.0).view(), "1.2E+01");
  EXPECT_EQ(microfmt::format<32>("{:.1a}", 1.0).view(), "0x1.0p+0");
  EXPECT_EQ(microfmt::format<32>("{:.2f}", static_cast<long double>(1.5L)).view(),
            "1.50");
}

TEST(FloatingFormatter, HandlesLargePrecisionWithoutTruncation) {
  const auto result = microfmt::format<192>("{:.150f}", 1.0);
  EXPECT_EQ(result.size(), 152U);
  EXPECT_TRUE(result.view().starts_with("1."));
}

TEST(BinaryTimeFormatter, FormatsAllPrecisionsSignsAndRawMode) {
  const bintime_like positive{1, UINT64_C(0x8000000000000000)};
  EXPECT_EQ(microfmt::format<32>("{}", positive).view(), "1.500000000s");
  EXPECT_EQ(microfmt::format<32>("{:m}", positive).view(), "1.500s");
  EXPECT_EQ(microfmt::format<32>("{:6R}", positive).view(), "1.500000");
  EXPECT_EQ(microfmt::format<32>("{:p}", positive).view(), "1.500000000000s");

  const bintime_like negative{-2, UINT64_C(0x8000000000000000)};
  EXPECT_EQ(microfmt::format<32>("{:3}", negative).view(), "-2.500s");

  const int64_t positive_sbt =
      (INT64_C(1) << 32) | INT64_C(0x80000000);
  EXPECT_EQ(microfmt::format<32>("{}", microfmt::as_sbintime(positive_sbt)).view(),
            "1.500000s");
  EXPECT_EQ(microfmt::format<32>(
                "{:mr}", microfmt::as_sbintime(-positive_sbt))
                .view(),
            "-1.500");
  EXPECT_EQ(microfmt::format<32>(
                "{:n}", microfmt::as_sbintime(positive_sbt))
                .view(),
            "1.500000000s");
}

TEST(BinaryTimeFormatter, ConvertsFractionalBoundaries) {
  EXPECT_EQ(microfmt::detail::bintime_frac_to_decimal(
                UINT64_C(0x8000000000000000), microfmt::time_precision::sec),
            0U);
  EXPECT_EQ(microfmt::detail::bintime_frac_to_decimal(
                UINT64_C(0x8000000000000000), microfmt::time_precision::ms),
            500U);
  EXPECT_EQ(microfmt::detail::bintime_frac_to_decimal(
                UINT64_C(0x8000000000000000), microfmt::time_precision::us),
            500000U);
  EXPECT_EQ(microfmt::detail::bintime_frac_to_decimal(
                UINT64_C(0x8000000000000000), microfmt::time_precision::ns),
            500000000U);
  EXPECT_EQ(microfmt::detail::bintime_frac_to_decimal(
                UINT64_C(0x8000000000000000), microfmt::time_precision::ps),
            UINT64_C(500000000000));
}

TEST(EscapedFormatter, CoversEveryControlEscapeAndQuoteMode) {
  const char controls[] = {'\0', '\a', '\b', '\t', '\n', '\v',
                           '\f', '\r', '\\', '"',  '\x1F', '\x7F'};
  const auto view =
      microfmt::escaped(microfmt::string_view(controls, sizeof(controls)));
  EXPECT_EQ(microfmt::format<128>("{}", view).view(),
            "\"\\0\\a\\b\\t\\n\\v\\f\\r\\\\\\\"\\x1f\\x7f\"");

  EXPECT_EQ(
      microfmt::format<32>(
          "{}", microfmt::escaped("\"", /*quote=*/false,
                                  /*escape_quotes=*/false))
          .view(),
      "\"");
}

TEST(FormatHelpers, FormatsExplicitRadicesAndByteUnits) {
  EXPECT_EQ(microfmt::format<32>("{}", microfmt::hex(0x2A, 4, true)).view(),
            "0x002a");
  EXPECT_EQ(microfmt::format<32>("{}", microfmt::hex(0xAB, 4, true, true)).view(),
            "0X00AB");
  EXPECT_EQ(microfmt::format<32>("{}", microfmt::bin(uint8_t{5}, 8, true)).view(),
            "0b00000101");

  EXPECT_EQ(microfmt::format<32>("{}", microfmt::bytes(0)).view(), "0 B");
  EXPECT_EQ(microfmt::format<32>("{}", microfmt::bytes(1024)).view(), "1 KiB");
  EXPECT_EQ(microfmt::format<32>("{}", microfmt::bytes(1536)).view(), "1.5 KiB");
  EXPECT_EQ(microfmt::format<32>("{}", microfmt::bytes(UINT64_MAX)).view(),
            "16383.9 PiB");
}

TEST(FormatHelpers, FormatsAddressRangesAlignmentAndJoins) {
  EXPECT_EQ(microfmt::format<64>("{}", microfmt::addr_offset(0x1140, 0x1000)).view(),
            "0x1000+0x140");
  EXPECT_EQ(microfmt::format<64>("{}", microfmt::addr_offset(0x0F00, 0x1000)).view(),
            "0x1000-0x100");
  EXPECT_EQ(microfmt::format<64>("{}", microfmt::mem_range(0x1000, 0x1400)).view(),
            "[0x1000..0x1400] (1 KiB)");
  EXPECT_EQ(microfmt::format<64>("{}", microfmt::mem_range(0x1400, 0x1000)).view(),
            "[0x1400..0x1000] (invalid)");

  EXPECT_EQ(microfmt::format<16>("{}", microfmt::align("x", 4)).view(), "x   ");
  EXPECT_EQ(microfmt::format<16>(
                "{}", microfmt::align("x", 4, microfmt::align_mode::right, '.'))
                .view(),
            "...x");
  EXPECT_EQ(microfmt::format<16>(
                "{}", microfmt::align("xy", 5, microfmt::align_mode::center, '.'))
                .view(),
            ".xy..");
  EXPECT_EQ(microfmt::format<16>("{}", microfmt::align("long", 2)).view(), "long");

  const int values[] = {1, 2, 3};
  EXPECT_EQ(microfmt::format<32>(
                "{}", microfmt::join(microfmt::span<const int>(values), "|"))
                .view(),
            "1|2|3");
  EXPECT_TRUE(microfmt::format<8>(
                  "{}", microfmt::join(microfmt::span<const int>{}, "|"))
                  .view()
                  .empty());
}

TEST(GridFormatter, FormatsRowsTitlesDefaultsAndNullViews) {
  const uint16_t values[] = {1, 0xAF, 0};
  const microfmt::reg_grid_desc<uint16_t, 3> description{
      "REGS", 2, {"CTRL", "STATUS", "X"}};
  EXPECT_EQ(microfmt::format<128>("{}",
                                 microfmt::make_reg_grid(values, description))
                .view(),
            "=== REGS ===\n"
            "CTRL = 0x0001  STATUS= 0x00AF\n"
            "X    = 0x0000\n");

  const microfmt::reg_grid_desc<uint16_t, 3> default_columns{
      "", 0, {"A", "B", "C"}};
  EXPECT_EQ(microfmt::format<128>(
                "{}", microfmt::make_reg_grid(values, default_columns))
                .view(),
            "A    = 0x0001  B    = 0x00AF  C    = 0x0000\n");

  EXPECT_TRUE(
      microfmt::format<16>("{}", microfmt::reg_grid_view<uint16_t, 3>{})
          .view()
          .empty());
}

TEST(HexdumpFormatter, FormatsDirectMemoryAndOptions) {
  const uint8_t bytes[] = {'A', 0, 0x7F};
  auto dump = microfmt::hexdump(microfmt::span<const uint8_t>(bytes), 0, 4);
  dump.show_address = false;
  EXPECT_EQ(microfmt::format<64>("{}", dump).view(),
            "41 00 7f     |A.. |\n");

  dump.uppercase = true;
  dump.show_ascii = false;
  EXPECT_EQ(microfmt::format<64>("{}", dump).view(), "41 00 7F    \n");

  microfmt::buffer_sink<128> output;
  microfmt::hexdump_to(output.as_sink(), microfmt::span<const uint8_t>(bytes),
                       0x10);
  EXPECT_TRUE(output.view().starts_with("0000000000000010: "));
}

TEST(HexdumpFormatter, MarksUnreadableCheckedMemory) {
  checked_reader_context reader{0x1000, {0x10, 0x11, 0x12, 0x13}, 2};
  auto dump =
      microfmt::hexdump_checked(reader.base, reader.bytes.size(), checked_reader,
                                &reader, 4, true);
  dump.show_address = false;
  EXPECT_EQ(microfmt::format<64>("{}", dump).view(),
            "10 11 ?? ??  |..??|\n");

  microfmt::buffer_sink<16> output;
  uint8_t scratch[4] = {};
  microfmt::format_hexdump(
      microfmt::hexdump_checked(0, 0, checked_reader, &reader),
      microfmt::span<uint8_t>(scratch), output.as_sink());
  microfmt::format_hexdump(dump, microfmt::span<uint8_t>{}, output.as_sink());
  EXPECT_TRUE(output.view().empty());
}

TEST(NetworkFormatter, FormatsMac48AndEui64Variants) {
  const uint8_t mac48[] = {0x00, 0x1A, 0x2B, 0xCC, 0xDD, 0xEF};
  EXPECT_EQ(microfmt::format<32>("{}", microfmt::mac(mac48)).view(),
            "00:1a:2b:cc:dd:ef");
  EXPECT_EQ(microfmt::format<32>("{:X-}", microfmt::mac(mac48)).view(),
            "00-1A-2B-CC-DD-EF");
  EXPECT_EQ(microfmt::format<32>("{:x.}", microfmt::mac(mac48, '-', true)).view(),
            "00.1a.2b.cc.dd.ef");
  EXPECT_EQ(microfmt::format<32>(
                "{}", microfmt::mac(microfmt::span<const uint8_t>(mac48), '\0'))
                .view(),
            "001a2bccddef");

  const std::array<uint8_t, 8> eui64 = {0, 1, 2, 3, 4, 5, 6, 7};
  EXPECT_EQ(microfmt::format<32>("{}", microfmt::mac(eui64, '_', true)).view(),
            "00_01_02_03_04_05_06_07");

  const uint8_t invalid[] = {1, 2, 3};
  EXPECT_EQ(microfmt::format<32>(
                "{}", microfmt::mac(microfmt::span<const uint8_t>(invalid)))
                .view(),
            "00:00:00:00:00:00");
}

TEST(PosixTimeFormatter, FormatsTimespecPrecisionAndUnits) {
  const timespec_like positive{12, 345678901};
  EXPECT_EQ(microfmt::format<32>("{}", positive).view(), "12.345678901s");
  EXPECT_EQ(microfmt::format<32>("{:m}", positive).view(), "12.345s");
  EXPECT_EQ(microfmt::format<32>("{:6R}", positive).view(), "12.345678");

  const timespec_like negative{-3, 7};
  EXPECT_EQ(microfmt::format<32>("{:n}", negative).view(), "-3.000000007s");
}

TEST(PosixTimeFormatter, FormatsTimevalPrecisionAndUnits) {
  const timeval_like positive{12, 345678};
  EXPECT_EQ(microfmt::format<32>("{}", positive).view(), "12.345678s");
  EXPECT_EQ(microfmt::format<32>("{:3r}", positive).view(), "12.345");

  const timeval_like negative{-3, 7};
  EXPECT_EQ(microfmt::format<32>("{:u}", negative).view(), "-3.000007s");
}

TEST(RegisterFormatter, FormatsDetailedShortNakedAndWideFields) {
  const microfmt::reg_descriptor<4> description{
      "CTRL",
      4,
      {{"READY", 0, 1, true},
       {"ERROR", 1, 1, true},
       {"MODE", 4, 4, false},
       {"ALL", 0, 64, false}}};

  EXPECT_EQ(microfmt::format<128>(
                "{}", microfmt::format_reg(uint32_t{0xA1}, description))
                .view(),
            "CTRL=0x000000A1 [READY, !ERROR, MODE=0xa, ALL=0xa1]");
  EXPECT_EQ(microfmt::format<128>(
                "{:s}", microfmt::format_reg(uint32_t{0xA0}, description))
                .view(),
            "CTRL=0x000000A0 [MODE=0xa, ALL=0xa0]");
  EXPECT_EQ(microfmt::format<128>(
                "{:N}", microfmt::format_reg(uint32_t{0}, description))
                .view(),
            "CTRL=0x00000000 !READY, !ERROR, MODE=0, ALL=0");

  EXPECT_TRUE(
      microfmt::format<16>("{}", microfmt::reg_view<4, uint32_t>{0, nullptr})
          .view()
          .empty());
}

TEST(StringFormatter, FormatsOwningStringsAlignmentAndPrecision) {
  const std::string owned = "value";
  EXPECT_EQ(microfmt::format<16>("{}", owned).view(), "value");
  EXPECT_EQ(microfmt::format<16>("{:8}", microfmt::as_string(owned)).view(),
            "value   ");
  EXPECT_EQ(microfmt::format<16>("{:>8.3}", microfmt::as_string("abcdef")).view(),
            "     abc");
  EXPECT_EQ(microfmt::format<16>("{:*^7}", microfmt::as_string("abc")).view(),
            "**abc**");
  EXPECT_EQ(microfmt::format<16>("{:<2}", microfmt::as_string("long")).view(),
            "long");
  EXPECT_EQ(microfmt::format<16>("{}", microfmt::as_string(nullptr)).view(),
            "(null)");
}

TEST(StringFormatter, DebugEscapesControlsAndUsesChunkedFill) {
  const char raw[] = {'a', '\n', '\r', '\t', '\\', '"', '\0', '\x01',
                      '\x7F'};
  const auto escaped =
      microfmt::as_string(microfmt::string_view(raw, sizeof(raw)));
  EXPECT_EQ(microfmt::format<64>("{:?}", escaped).view(),
            "\"a\\n\\r\\t\\\\\\\"\\0\\x01\\x7f\"");

  const auto padded =
      microfmt::format<64>("{:*>40}", microfmt::as_string("x"));
  EXPECT_EQ(padded.size(), 40U);
  EXPECT_EQ(padded.view().back(), 'x');
}
