// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/formatters/errno.hpp>
#include <microfmt/sinks/stdio.hpp>

#include <cerrno>
#include <cstdio>

int main() {
  microfmt::println("Explicit: {}", microfmt::format_errno(ENOENT));
  microfmt::println("Explicit: {}", microfmt::format_errno(EACCES));

  std::FILE *file = std::fopen("/nonexistent/path/file.txt", "r");
  if (!file) {
    microfmt::println("Current:  {}", microfmt::current_errno());
  }

  return 0;
}
