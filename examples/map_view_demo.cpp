// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <microfmt/formatters/map_view.hpp>
#include <microfmt/microfmt.hpp>
#include <string_view>
#include <utility>

namespace {

void stdout_write(void * /*context*/, microfmt::string_view text) noexcept {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  std::fwrite(text.data(), 1, text.size(), stdout);

  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

} // namespace

int main() {
  const microfmt::sink output{nullptr, stdout_write};
  const std::map<uint16_t, uint16_t> registers{{0x0001, 0x00A1}, {0x000F, 0xBEEF}};
  const std::array<std::pair<uint8_t, int>, 3> rails{{{1, 3305}, {2, 1812}, {3, 5004}}};

  microfmt::format_to(output, "registers: {}\n", microfmt::map_view(registers));
  microfmt::format_to(output, "hex registers: {:b04X}\n", microfmt::map_view(registers));

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  microfmt::format_to(output, "rail slice: {}\n",
                      microfmt::map_view(
                          rails.begin() + 1, rails.end(), [](const auto &rail) noexcept { return rail.first; },
                          [](const auto &rail) noexcept { return rail.second; }));

  RELOCO_END_UNSAFE_BUFFER_USAGE;
}
