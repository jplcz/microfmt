// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// A recursive-descent JSON validator built on microfmt::token_stream.
// Each grammar rule is an ordinary function returning expected<void, parse_error>.

#include <cstdio>

#include <microfmt/microfmt.hpp>
#include <microfmt/parsing/lexer_rules_presets.hpp>
#include <microfmt/parsing/token_stream.hpp>

namespace {

using microfmt::token_kind;
using result = reloco::expected<void, microfmt::parse_error>;

constexpr size_t kMaxDepth = 32; // nesting limit so hostile input cannot overflow the stack

// JSON numbers may start with '-', which the json preset leaves as an error byte; lex it as an operator.
constexpr microfmt::string_view kMinus[] = {"-"};

microfmt::lexer_rules json_rules() {
  microfmt::lexer_rules rules = microfmt::rules::json;
  rules.operators = kMinus;
  return rules;
}

result parse_value(microfmt::token_stream &ts, size_t &depth);

// array  := '[' [ value { ',' value } ] ']'
// object := '{' [ string ':' value { ',' string ':' value } ] '}'
// parse_list handles brackets, separators and the empty list; we only describe one element.
result parse_container(microfmt::token_stream &ts, size_t &depth, bool object) {
  microfmt::list_options brackets;
  brackets.open = object ? "{" : "[";
  brackets.close = object ? "}" : "]";
  return microfmt::parse_list(ts, brackets, [&](microfmt::token_stream &s) -> result {
    if (object) {
      if (auto key = s.expect(token_kind::string, "expected a string key"); !key) {
        return reloco::unexpected<microfmt::parse_error>(key.error());
      }
      if (auto colon = s.expect(token_kind::punctuation, ":", "expected ':'"); !colon) {
        return reloco::unexpected<microfmt::parse_error>(colon.error());
      }
    }
    return parse_value(s, depth);
  });
}

result parse_value(microfmt::token_stream &ts, size_t &depth) {
  microfmt::depth_guard guard{depth, kMaxDepth};
  if (!guard) {
    return reloco::unexpected<microfmt::parse_error>(ts.error_here("nesting too deep"));
  }
  if (ts.check(token_kind::punctuation, "{")) {
    return parse_container(ts, depth, true);
  }
  if (ts.check(token_kind::punctuation, "[")) {
    return parse_container(ts, depth, false);
  }
  ts.skip(token_kind::op, "-");
  if (ts.skip(token_kind::string) || ts.skip(token_kind::integer) || ts.skip(token_kind::floating) ||
      ts.skip(token_kind::keyword)) {
    return {};
  }
  return reloco::unexpected<microfmt::parse_error>(ts.error_here("expected a value"));
}

result validate(microfmt::string_view text) {
  microfmt::token_stream ts{text, json_rules()};
  size_t depth = 0;
  if (auto value = parse_value(ts, depth); !value) {
    return value;
  }
  if (!ts.at_end()) {
    return reloco::unexpected<microfmt::parse_error>(ts.error_here("trailing input"));
  }
  return {};
}

} // namespace

int main() {
  constexpr microfmt::string_view docs[] = {
      "{\"name\": \"microfmt\", \"tags\": [1, -2.5, true, null]}",
      "{\"a\": [1, 2,, 3]}",
      "[1, 2] extra",
  };

  microfmt::buffer_sink<1024> out;
  for (const microfmt::string_view doc : docs) {
    const result r = validate(doc);
    if (r) {
      microfmt::format_to(out.as_sink(), "ok:    {}\n", doc);
    } else {
      const microfmt::parse_error &e = r.error();
      microfmt::format_to(out.as_sink(), "error: {} at {}:{} in {}\n", e.message, e.at.line, e.at.column, doc);
    }
  }

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  std::printf("%.*s", static_cast<int>(out.view().size()), out.view().data());

  RELOCO_END_UNSAFE_BUFFER_USAGE;
}
