// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <cstdio>

#include <microfmt/formatters/base_views.hpp>
#include <microfmt/microfmt.hpp>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

int main() {
  const uint8_t payload[] = {'M', 'a', 'n'};
  const auto encoded = microfmt::format<32>("base64={}", microfmt::base64(payload));
  const auto bits = microfmt::format<32>("bits={}", microfmt::bin_grouped(uint8_t{0xa5}, 2, ':'));

  std::printf("%.*s\n%.*s\n", static_cast<int>(encoded.size()), encoded.view().data(), static_cast<int>(bits.size()),
              bits.view().data());
}

RELOCO_END_UNSAFE_BUFFER_USAGE
