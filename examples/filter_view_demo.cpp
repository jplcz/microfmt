// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <array>
#include <cstdint>
#include <cstdio>
#include <microfmt/formatters/filter_view.hpp>
#include <microfmt/microfmt.hpp>
#include <string_view>

namespace {

void stdout_write(void * /*context*/, std::string_view text) noexcept {
  std::fwrite(text.data(), 1, text.size(), stdout);
}

} // namespace

int main() {
  const microfmt::sink output{nullptr, stdout_write};
  const std::array<int16_t, 6> temperatures{{-12, 18, 42, 77, 93, 105}};
  const uint16_t registers[] = {0x0001, 0x000A, 0x001F, 0x00B0};

  microfmt::format_to(
      output, "safe temperatures: {}\n",
      microfmt::filter(temperatures, [](int16_t value) noexcept {
        return value >= 0 && value <= 85;
      }));
  microfmt::format_to(
      output, "high registers: {:c04X}\n",
      microfmt::filter(registers, [](uint16_t value) noexcept {
        return value >= 0x0010;
      }));
  microfmt::format_to(
      output, "odd register values: {:n02x}\n",
      microfmt::filter(registers, size_t{4}, [](uint16_t value) noexcept {
        return value % 2 != 0;
      }));
}
