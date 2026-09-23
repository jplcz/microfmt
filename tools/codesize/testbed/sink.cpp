// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Out-of-line sink implementation, compiled as its own translation unit so
// every module's microfmt::format_to call is an opaque function call the
// optimizer cannot fold away or see through -- matching how a real
// logging/UART transport is normally a separate compilation unit. Output is
// discarded (written to /dev/null, falling back to stdout); only code size
// is under test here, not runtime behavior.

#include "common.hpp"

#include <cstdio>

namespace {

std::FILE *discard_file() noexcept {
  static std::FILE *file = std::fopen("/dev/null", "w");
  return file != nullptr ? file : stdout;
}

void discard_write(void * /*ctx*/, microfmt::string_view sv) noexcept {
  if (!sv.empty()) {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
    std::fwrite(sv.data(), 1, sv.size(), discard_file());
    RELOCO_END_UNSAFE_BUFFER_USAGE;
  }
}

} // namespace

namespace testbed {

microfmt::sink &backend_sink() noexcept {
  static microfmt::sink instance{nullptr, &discard_write};
  return instance;
}

} // namespace testbed
