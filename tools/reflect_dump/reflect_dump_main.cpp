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

// With no arguments, writes to stdout (shell-redirect it as shown in
// README.md). With one argument, writes directly to that file path
// instead -- used by cmake/ReflectDump.cmake so the build does not have
// to rely on shell redirection support, which is not portable across
// every CMake generator/host combination.
int main(int argc, char **argv) {
  const std::string generated = ::microfmt::detail::render_reflect_dump();

  std::FILE *out = stdout;
  if (argc > 1) {
    out = std::fopen(argv[1], "wb");
    if (out == nullptr) {
      std::fprintf(stderr, "reflect_dump: failed to open '%s' for writing\n", argv[1]);
      return 1;
    }
  }

  std::fwrite(generated.data(), 1, generated.size(), out);

  if (out != stdout) {
    std::fclose(out);
  }
  return 0;
}
