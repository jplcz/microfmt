// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdio>

#include <microfmt/formatters/math.hpp>
#include <microfmt/microfmt.hpp>

MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE

int main() {
  const int transform[] = {1, 0, 0, 1};
  const auto vector = microfmt::format<32>("position={}", microfmt::vec3(4, 8, 15));
  const auto matrix = microfmt::format<64>("transform={}", microfmt::mat<int, 2, 2>(transform));

  std::printf("%.*s\n%.*s\n", static_cast<int>(vector.size()), vector.view().data(), static_cast<int>(matrix.size()),
              matrix.view().data());
}

MICROFMT_END_UNSAFE_BUFFER_USAGE
