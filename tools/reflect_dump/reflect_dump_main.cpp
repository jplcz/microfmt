// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Reflection dump code generator: builds only with a P2996-capable
// compiler and `-std=c++26 -freflection -DMICROFMT_REFLECT_DUMP_MODE`; see
// docs/reflection.md for the full writeup and exact build commands.
//
// Include every header that defines a type annotated with
// `MICROFMT_REFLECT_FORMAT`/`MICROFMT_REFLECT_DUMP_ENUM` (see
// `microfmt/formatters/reflect_annotate.hpp`) by passing
// `-DMICROFMT_REFLECT_DUMP_MANIFEST=\"your_types.hpp\"` on the command
// line, or edit the `#include` below directly. `example_manifest.hpp`
// demonstrates the expected shape.

#include <microfmt/formatters/reflect_annotate.hpp>

#ifndef MICROFMT_REFLECT_DUMP_MANIFEST
#define MICROFMT_REFLECT_DUMP_MANIFEST "example_manifest.hpp"
#endif

#include MICROFMT_REFLECT_DUMP_MANIFEST

#include <cstdio>

int main() {
  const std::string generated = ::microfmt::detail::render_reflect_dump();
  std::fwrite(generated.data(), 1, generated.size(), stdout);
  return 0;
}
