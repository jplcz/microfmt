// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdio>
#include <microfmt/formatters/fixed_point.hpp>
#include <microfmt/microfmt.hpp>

int main() {
  int32_t vcc_mv = 3295;
  int32_t current_ua = 14200;
  int16_t subzero_temp_cdeg = -45; // -0.45 C

  auto out = microfmt::format<128>("VCC: {} V, Current: {} mA, Temp: {} C", microfmt::fixed<1000, 2>(vcc_mv), // "3.29"
                                   microfmt::fixed<1000, 3>(current_ua),      // "14.200"
                                   microfmt::fixed<100, 2>(subzero_temp_cdeg) // "-0.45"
  );

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  std::fwrite(out.view().data(), 1, out.size(), stdout);

  RELOCO_END_UNSAFE_BUFFER_USAGE;

  std::putchar('\n');
}
