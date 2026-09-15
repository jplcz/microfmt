// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/formatters/fixed_point.hpp>
#include <microfmt/microfmt.hpp>
#include <microfmt/sinks/stdio.hpp>

int main() {
  // Standard stdout & stderr formatting
  microfmt::print("System init: {:s}\n", "starting");
  microfmt::println(stderr, "Log [WARN]: rail voltage low ({} V)",
                    microfmt::milli(3120));

  // Composing sinks
  auto out = microfmt::stdout_sink();
  microfmt::format_to(out, "Core Clock: {} MHz\n", 168);

#if MICROFMT_HAS_POSIX_FD
  // Direct unbuffered POSIX fd output (STDOUT_FILENO = 1)
  microfmt::println(STDOUT_FILENO, "Direct syscall write on fd={}", 1);
#endif

  return 0;
}
