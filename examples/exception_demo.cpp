// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/formatters/exception.hpp>
#include <microfmt/sinks/stdio.hpp>

#include <stdexcept>
#include <system_error>

int main() {
  try {
    throw std::runtime_error("something went wrong");
  } catch (const std::exception &ex) {
    microfmt::println("Caught: {}", ex);
  }

  try {
    throw std::system_error(std::make_error_code(std::errc::permission_denied), "cannot open file");
  } catch (const std::system_error &ex) {
    microfmt::println("Caught: {}", ex);
  }

  return 0;
}
