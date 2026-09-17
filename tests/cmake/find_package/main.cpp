// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/microfmt.hpp>

int main() {
  microfmt::buffer_sink<32> output;
  microfmt::format_to(output.as_sink(), MICROFMT_STRING("{}"), 42);
  return output.view() == "42" ? 0 : 1;
}
