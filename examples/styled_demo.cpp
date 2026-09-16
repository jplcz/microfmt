// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdio>
#include <microfmt/formatters/styled.hpp>
#include <microfmt/microfmt.hpp>
#include <string_view>

namespace {
MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE

void stdout_write(void * /*context*/, microfmt::string_view text) noexcept {
  std::fwrite(text.data(), 1, text.size(), stdout);
}

MICROFMT_END_UNSAFE_BUFFER_USAGE

} // namespace

int main() {
  const microfmt::sink output{nullptr, stdout_write};

  microfmt::format_to(output, "=== Styled Text Views ===\n");
  microfmt::format_to(output, "left=[{}], center=[{}], right=[{}]\n", microfmt::pad("UART", 10),
                      microfmt::pad_center("READY", 10, '.'), microfmt::pad_right("OK", 10, '_'));

  microfmt::format_to(output, "upper={}, lower={}, title={:t}\n", microfmt::to_upper("boot complete"),
                      microfmt::to_lower("VCC_RAIL"), microfmt::to_lower("SENSOR_STATUS"));

  microfmt::format_to(output, "quoted={}, bracketed={:b}, shortened={}\n",
                      microfmt::quoted("eth0", microfmt::quote_style::single_quotes), microfmt::to_upper("connected"),
                      microfmt::truncate("temperature-sensor-channel-0", 14));

  microfmt::format_to(output, "specifier controls: |{:*^18u}| |{:>18tq.10}|\n", microfmt::pad_right("system online", 4),
                      microfmt::to_lower("FIRMWARE_UPDATE_PENDING"));
  return 0;
}
