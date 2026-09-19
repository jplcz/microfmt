// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/formatters/error.hpp>
#include <microfmt/sinks/stdio.hpp>

int main() {
  const std::error_code not_found = std::make_error_code(std::errc::no_such_file_or_directory);
  const std::error_code generic = std::make_error_code(std::errc::invalid_argument);

  microfmt::println("Category: {}", not_found.category());
  microfmt::println("Error:    {}", not_found);
  microfmt::println("Error:    {}", generic);

  return 0;
}
