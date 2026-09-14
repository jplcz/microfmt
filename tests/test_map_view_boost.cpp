// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <boost/intrusive/set.hpp>
#include <microfmt/formatters/map_view.hpp>
#include <microfmt/microfmt.hpp>

namespace {

struct sensor_node : boost::intrusive::set_base_hook<> {
  int channel;
  int millivolts;

  sensor_node(int channel_value, int millivolt_value) noexcept
      : channel(channel_value), millivolts(millivolt_value) {}

  bool operator<(const sensor_node &other) const noexcept {
    return channel < other.channel;
  }
};

} // namespace

TEST(MapViewBoostTest, FormatsIntrusiveSetWithCustomExtractors) {
  sensor_node rail_5v{5, 5004};
  sensor_node rail_1v8{2, 1812};
  sensor_node rail_3v3{3, 3305};
  boost::intrusive::set<sensor_node> rails;
  rails.insert(rail_5v);
  rails.insert(rail_1v8);
  rails.insert(rail_3v3);

  microfmt::buffer_sink<128> output;
  microfmt::format_to(
      output.as_sink(), "{}",
      microfmt::map_view(
          rails, [](const sensor_node &node) noexcept { return node.channel; },
          [](const sensor_node &node) noexcept { return node.millivolts; }));

  EXPECT_EQ(output.view(), "{2: 1812, 3: 3305, 5: 5004}");
  rails.clear();
}
