// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Packrat memoization: the grammar `A := "(" A ")" "!" | "(" A ")" "?" | "x"` re-parses the inner A for the
// second alternative, so the work doubles with every nesting level. `memoize` remembers the outcome of A at
// each position, which turns the repeat into a lookup. The demo prints how many times the rule body ran.

#include <cstdio>

#include <microfmt/microfmt.hpp>
#include <microfmt/parsing/token_stream.hpp>

namespace {

using microfmt::parse_error;
using microfmt::string_view;
using microfmt::token_kind;
using microfmt::token_stream;

using depth_result = reloco::expected<int, parse_error>;

constexpr string_view kSuffixes[] = {"!", "?"};

struct grammar {
  bool use_memo{false};
  size_t body_runs{0};
  // 64 slots, one result type (nesting depth). A collision only costs a re-parse, never a wrong answer.
  microfmt::memo_table<int, 64> memo{};

  depth_result group(token_stream &ts) {
    if (!use_memo) {
      return body(ts);
    }
    // Rule id 1 identifies this rule in the table; the rule must depend only on the input position.
    return microfmt::memoize(ts, memo, 1, [this](token_stream &s) { return body(s); });
  }

  depth_result wrapped(token_stream &ts, string_view suffix) {
    if (auto open = ts.expect(token_kind::punctuation, "(", "expected '('"); !open) {
      return reloco::unexpected<parse_error>(open.error());
    }
    auto inner = group(ts);
    if (!inner) {
      return inner;
    }
    if (auto close = ts.expect(token_kind::punctuation, ")", "expected ')'"); !close) {
      return reloco::unexpected<parse_error>(close.error());
    }
    if (auto tail = ts.expect(token_kind::op, suffix, "expected a suffix"); !tail) {
      return reloco::unexpected<parse_error>(tail.error());
    }
    return inner.value() + 1;
  }

  depth_result body(token_stream &ts) {
    ++body_runs;
    return microfmt::parse_choice(
        ts, [this](token_stream &s) -> depth_result { return wrapped(s, "!"); },
        [this](token_stream &s) -> depth_result { return wrapped(s, "?"); },
        [](token_stream &s) -> depth_result {
          if (auto x = s.expect(token_kind::identifier, "x", "expected 'x'"); !x) {
            return reloco::unexpected<parse_error>(x.error());
          }
          return 0;
        });
  }
};

} // namespace

int main() {
  // Depth 17: the `?` alternative matches at every level, so the `!` alternative always fails first.
  constexpr string_view input = "(((((((((((((((((x)?)?)?)?)?)?)?)?)?)?)?)?)?)?)?)?)?";

  microfmt::lexer_rules rules;
  rules.operators = kSuffixes;

  microfmt::buffer_sink<512> out;
  for (const bool use_memo : {false, true}) {
    grammar g;
    g.use_memo = use_memo;
    token_stream ts{input, rules};
    const depth_result result = g.group(ts);
    microfmt::format_to(out.as_sink(), "{}: depth {}, rule body ran {} times",
                        use_memo ? "with memo   " : "without memo", result ? result.value() : -1, g.body_runs);
    if (use_memo) {
      microfmt::format_to(out.as_sink(), " ({} cache hits)", g.memo.hits());
    }
    microfmt::format_to(out.as_sink(), "\n");
  }

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  std::printf("%.*s", static_cast<int>(out.view().size()), out.view().data());

  RELOCO_END_UNSAFE_BUFFER_USAGE;
}
