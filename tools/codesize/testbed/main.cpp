// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Executable harness: links every generated module directly into a static
// binary, the "many .cpp files in one executable" scenario.

#include "modules.hpp"

int main() {
  testbed::run_all_modules(testbed::backend_out());
  return 0;
}
