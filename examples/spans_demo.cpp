// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Dotted paths such as `config.server.port`: parse_chain reads `name ("." name)*` and parse_spanned
// reports where each path sits in the source, e.g. for later "unknown key" diagnostics.

#include <cstdio>

#include <microfmt/microfmt.hpp>
#include <microfmt/parsing/token_stream_formatters.hpp>

namespace {

using microfmt::parse_error;
using microfmt::string_view;
using microfmt::token_kind;
using microfmt::token_stream;

using depth_result = reloco::expected<size_t, parse_error>;

// Parses one path and returns how many names it has. The chain value is just a counter here;
// use a node index instead to build a tree.
depth_result path_length(token_stream &ts) {
  // One path segment: a bare name. The error is reported at the token that was found instead.
  auto name = [](token_stream &s) -> depth_result {
    if (auto id = s.expect(token_kind::identifier, "expected a name"); !id) {
      return reloco::unexpected<parse_error>(id.error());
    }
    return size_t{1};
  };
  // Called once per "." with the segments so far on the left and the new one on the right.
  auto fold = [](const microfmt::token &, size_t left, size_t right) -> depth_result { return left + right; };
  // max_items = 4 stops absurdly long paths.
  return microfmt::parse_chain<size_t>(ts, token_kind::punctuation, ".", name, fold, 4);
}

} // namespace

int main() {
  constexpr string_view source = "config.server.port\n  logging.level\n  a.b.\"x\"\n  x.y.z.w.v";

  microfmt::lexer_rules rules;
  rules.punctuation = ".";
  token_stream ts{source, rules};

  microfmt::buffer_sink<1024> out;
  while (!ts.at_end()) {
    // parse_spanned pairs the value (segment count) with the exact source range of the path.
    auto path = microfmt::parse_spanned(ts, path_length);
    if (path) {
      microfmt::format_to(out.as_sink(), "{}:{}  '{}'  {} segments\n", path.value().span.line, path.value().span.column,
                          path.value().span.text, path.value().value);
      continue;
    }
    microfmt::format_to(out.as_sink(), "{}\n", microfmt::report(path.error(), source));
    // Skip the offending token so the loop always makes progress, then try the next path.
    ts.next();
  }

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  std::printf("%.*s\n", static_cast<int>(out.view().size()), out.view().data());

  RELOCO_END_UNSAFE_BUFFER_USAGE;
}
