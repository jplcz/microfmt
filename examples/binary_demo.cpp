// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <cstdio>
#include <microfmt/formatters/binary.hpp>
#include <microfmt/microfmt.hpp>

int main() {
  uint8_t cr1 = 0b00101100;
  uint16_t timer_psc = 0x0FA0;

  // Natural width
  auto s1 = microfmt::format<64>("CR1: {}", microfmt::bin(cr1));
  // CR1: 00101100

  // Grouped 16-bit word with prefix
  auto s2 = microfmt::format<64>("PSC: {}", microfmt::bin_prefixed(timer_psc, /*group_nibbles=*/true));
  // PSC: 0b0000_1111_1010_0000

  // Slice lowest 3 bits
  auto s3 = microfmt::format<64>("Clock Div: {}", microfmt::bin<3>(cr1));
  // Clock Div: 100

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  std::fwrite(s1.view().data(), 1, s1.size(), stdout);
  std::putchar('\n');
  std::fwrite(s2.view().data(), 1, s2.size(), stdout);
  std::putchar('\n');
  std::fwrite(s3.view().data(), 1, s3.size(), stdout);
  std::putchar('\n');

  RELOCO_END_UNSAFE_BUFFER_USAGE;
}
