// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <cstdio>
#include <microfmt/formatters/tuple.hpp>
#include <microfmt/microfmt.hpp>
#include <string_view>
#include <tuple>
#include <utility>

namespace {

void stdout_write(void * /*context*/, std::string_view text) noexcept {
  std::fwrite(text.data(), 1, text.size(), stdout);
}

} // namespace

int main() {
  const microfmt::sink output{nullptr, stdout_write};
  const auto telemetry = std::make_tuple("temperature", 42, "C");
  const std::tuple<uint16_t, uint16_t, uint16_t> registers{
      0x00A1, 0x000F, 0xBEEF};

  microfmt::format_to(output, "default: {}\n", telemetry);
  microfmt::format_to(output, "pair:    {:b}\n", std::make_pair("CAN", 0x123));
  microfmt::format_to(output, "registers: {:c04X}\n", registers);
  microfmt::format_to(output, "bare hexadecimal IDs: {:n03x}\n",
                      std::make_tuple(0x12, 0x2AB, 0x7));
}
