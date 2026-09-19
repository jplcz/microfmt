// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <microfmt/formatters/fmt.hpp>
#include <microfmt/microfmt.hpp>

// User domain type with a standard fmt::formatter specialization
struct GeoCoord {
  int32_t lat_deg;
  int32_t lon_deg;
};

template <> struct fmt::formatter<GeoCoord> {
  constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }

  template <typename FormatContext> auto format(const GeoCoord &c, FormatContext &ctx) const {
    auto out = ctx.out();
    // Emits characters directly through the sink_output_iterator
    auto str = fmt::format("Lat: {}, Lon: {}", c.lat_deg, c.lon_deg);
    for (char ch : str.view()) {
      *out++ = ch;
    }
    return out;
  }
};

int main() {
  GeoCoord pos{52, 21};

  // Uses microfmt formatting pipeline transparently
  auto buf = microfmt::format<128>("Location: [{}]", pos);

  // Output: Location: [Lat: 52, Lon: 21]
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  std::fwrite(buf.view().data(), 1, buf.size(), stdout);

  RELOCO_END_UNSAFE_BUFFER_USAGE;

  std::putchar('\n');

  return 0;
}
