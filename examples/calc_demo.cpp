// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// A small integer calculator: operator tables + parse_expression, with errors shown by error_report.

#include <cstdint>
#include <cstdio>

#include <microfmt/microfmt.hpp>
#include <microfmt/parsing/number_parse.hpp>
#include <microfmt/parsing/token_stream_formatters.hpp>

namespace {

using calc_result = reloco::expected<int64_t, microfmt::parse_error>;

// Operator spellings the lexer must emit as `op` tokens.
constexpr microfmt::string_view kOperators[] = {"+", "-", "*", "/", "^"};

// Higher precedence binds tighter; `^` is right associative so 2^3^2 == 2^(3^2).
constexpr microfmt::binary_op kBinary[] = {{"+", 10}, {"-", 10}, {"*", 20}, {"/", 20}, {"^", 40, microfmt::assoc::right}};

// Unary minus binds tighter than `*` but looser than `^`, so -2^2 == -(2^2).
constexpr microfmt::prefix_op kPrefix[] = {{"-", 30}};

calc_result evaluate(microfmt::string_view text) {
  microfmt::lexer_rules rules;
  rules.operators = kOperators;
  microfmt::token_stream ts{text, rules};

  // Parses the leaves of the expression; "(" expr ")" never reaches this callback.
  auto primary = [](microfmt::token_stream &s) -> calc_result {
    if (!s.check(microfmt::token_kind::integer)) {
      return reloco::unexpected<microfmt::parse_error>(s.error_here("expected a number"));
    }
    return microfmt::parse<int64_t>(s.next().text).value();
  };

  // Combines two already-evaluated operands; build an AST node here instead to get a tree.
  auto binary = [&ts](const microfmt::binary_op &op, int64_t l, int64_t r) -> calc_result {
    switch (op.text[0]) {
    case '+':
      return l + r;
    case '-':
      return l - r;
    case '*':
      return l * r;
    case '/':
      if (r == 0) {
        return reloco::unexpected<microfmt::parse_error>(ts.error_here("division by zero"));
      }
      return l / r;
    default: {
      int64_t result = 1;
      for (int64_t i = 0; i < r; ++i) {
        result *= l;
      }
      return result;
    }
    }
  };

  auto prefix = [](const microfmt::prefix_op &, int64_t v) -> calc_result { return -v; };

  auto result = microfmt::parse_expression<int64_t>(ts, kBinary, kPrefix, primary, binary, prefix);
  if (result && !ts.at_end()) {
    return reloco::unexpected<microfmt::parse_error>(ts.error_here("unexpected input after the expression"));
  }
  return result;
}

} // namespace

int main() {
  constexpr microfmt::string_view inputs[] = {"1 + 2 * 3", "-(2 + 3) ^ 2", "2 ^ 3 ^ 2", "4 * (3 +", "7 7"};

  microfmt::buffer_sink<1024> out;
  for (const microfmt::string_view input : inputs) {
    const calc_result r = evaluate(input);
    if (r) {
      microfmt::format_to(out.as_sink(), "{} = {}\n", input, r.value());
    } else {
      microfmt::format_to(out.as_sink(), "{}\n", microfmt::report(r.error(), input));
    }
  }

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  std::printf("%.*s\n", static_cast<int>(out.view().size()), out.view().data());

  RELOCO_END_UNSAFE_BUFFER_USAGE;
}
