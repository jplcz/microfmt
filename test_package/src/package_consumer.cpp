// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/microfmt.hpp>

int main() {
  const auto output = microfmt::format<16>(MICROFMT_STRING("{}"), 42);
  return output.view() == "42" ? 0 : 1;
}
