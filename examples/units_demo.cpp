// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <microfmt/formatters/units.hpp>
#include <microfmt/sinks/stdio.hpp>

int main() {
  microfmt::println("Current:    {}", microfmt::with_unit(42, "mA"));
  microfmt::println("Frequency:  {}", microfmt::hertz(50000000));
  microfmt::println("Voltage:    {:.1}", microfmt::auto_si(3300, "V"));
  microfmt::println("Firmware:   {}", microfmt::auto_bytes(1572864));

  return 0;
}
