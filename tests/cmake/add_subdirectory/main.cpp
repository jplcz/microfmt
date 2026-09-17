// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/microfmt.hpp>

int main() {
  const auto output =
      microfmt::format<32>(MICROFMT_STRING("value={}"), 42);
  return output.view() == "value=42" ? 0 : 1;
}
