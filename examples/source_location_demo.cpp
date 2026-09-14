// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <cstdio>

#include <microfmt/formatters/source_location.hpp>
#include <microfmt/microfmt.hpp>

int main() {
#if MICROFMT_HAS_STD_SOURCE_LOCATION
  const auto message =
      microfmt::format<256>("called from {:s}", microfmt::source_loc());
  std::printf("%.*s\n", static_cast<int>(message.size()), message.view().data());
#else
  std::puts("std::source_location requires C++20 support.");
#endif
}
