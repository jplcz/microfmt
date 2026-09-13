#include <gtest/gtest.h>

#include <array>
#include <map>
#include <microfmt/formatters/map_view.hpp>
#include <microfmt/microfmt.hpp>
#include <utility>

TEST(MapViewTest, FormatsStandardMapWithDefaultExtractors) {
  microfmt::buffer_sink<128> output;
  const std::map<int, const char *> states{
      {1, "boot"}, {3, "ready"}, {7, "fault"}};

  microfmt::format_to(output.as_sink(), "{}", microfmt::map_view(states));

  EXPECT_EQ(output.view(), "{1: boot, 3: ready, 7: fault}");
}

TEST(MapViewTest, SupportsDelimiterAndForwardedElementSpecifiers) {
  microfmt::buffer_sink<128> output;
  const std::map<uint16_t, uint16_t> registers{
      {0x0001, 0x00A1}, {0x000F, 0xBEEF}};

  microfmt::format_to(output.as_sink(), "{:b04X}",
                      microfmt::map_view(registers));
  EXPECT_EQ(output.view(), "[0001: 00A1, 000F: BEEF]");

  output.reset();
  microfmt::format_to(output.as_sink(), "{:n02x}",
                      microfmt::map_view(registers));
  EXPECT_EQ(output.view(), "01: a1, 0f: beef");
}

TEST(MapViewTest, SupportsIteratorRangesAndCustomExtractors) {
  microfmt::buffer_sink<128> output;
  const std::array<std::pair<uint8_t, int>, 3> samples{
      {{2, 3305}, {3, 3080}, {4, 1812}}};

  const auto view = microfmt::map_view(
      samples.begin() + 1, samples.end(),
      [](const auto &sample) noexcept { return sample.first; },
      [](const auto &sample) noexcept { return sample.second; });
  microfmt::format_to(output.as_sink(), "{:c}", view);

  EXPECT_EQ(output.view(), "{3: 3080, 4: 1812}");
}
