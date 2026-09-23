// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Shared-library harness: the same generated modules built into a .so with
// a single exported C entry point, the "many .cpp files in one shared
// library" scenario (distinct from the executable: different relocation
// model, .dynsym, and (for ELF) usually -fPIC).

#include "modules.hpp"

extern "C" void testbed_run() { testbed::run_all_modules(testbed::backend_out()); }
