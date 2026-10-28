// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Runs the mini language from mini_lang.hpp on two programs: one valid, one with several mistakes
// (all reported in a single run, with source snippets and a note on the unclosed block).

#include <cstdio>

#include "mini_lang.hpp"

namespace {

void run(microfmt::string_view title, microfmt::string_view program, microfmt::buffer_sink<2048> &out) {
  mini_lang::interpreter interp{program};
  const size_t ok = interp.run();
  microfmt::format_to(out.as_sink(), "== {} ==\n{}", title, interp.output());
  microfmt::format_to(out.as_sink(), "{} statements ok, {} errors\n", ok, interp.errors().total());
  const auto stored = interp.errors().errors();
  for (size_t i = 0; i < stored.size(); ++i) {
    microfmt::format_to(out.as_sink(), "{}\n", microfmt::report(stored[i], program));
  }
}

} // namespace

int main() {
  constexpr microfmt::string_view good = "let x = 2 + 3 * 4;\n"
                                         "let y = x > 10 ? x : 0;\n"
                                         "print(max(x, y, 7));\n"
                                         "{ let z = -y; print(z); }\n";

  constexpr microfmt::string_view bad = "let a = 1;\n"
                                        "let b = 1 +;\n"
                                        "print(a;\n"
                                        "{ let c = nope; print(a); }\n"
                                        "print(abs(-5));\n"
                                        "{ let d = 1;\n";

  microfmt::buffer_sink<2048> out;
  run("valid", good, out);
  run("broken", bad, out);

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  std::printf("%.*s", static_cast<int>(out.view().size()), out.view().data());

  RELOCO_END_UNSAFE_BUFFER_USAGE;
}
