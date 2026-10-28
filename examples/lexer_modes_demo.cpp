// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Lexer modes: the text of an interpolated string `"total: ${1 + 2} items"` is raw, but inside `${ ... }` it is
// ordinary code. The parser knows which one it is in, so it switches the lexer rules for the string
// (`with_rules`) and asks for raw text where text is expected (`take_raw_until`).

#include <cstdio>
#include <string>

#include <microfmt/microfmt.hpp>
#include <microfmt/parsing/token_stream.hpp>
#include <microfmt/parsing/token_stream_formatters.hpp>

namespace {

using microfmt::parse_error;
using microfmt::string_view;
using microfmt::token_kind;
using microfmt::token_stream;

using rendered = reloco::expected<std::string, parse_error>;

constexpr string_view kPlus[] = {"+"};

// `n ( "+" n )*`; the value is the sum, so `${1 + 2}` renders as 3.
reloco::expected<long long, parse_error> parse_sum(token_stream &ts) {
  long long total = 0;
  for (;;) {
    auto number = ts.expect(token_kind::integer, "expected a number");
    if (!number) {
      return reloco::unexpected<parse_error>(number.error());
    }
    long long value = 0;
    for (const char digit : number.value().text) {
      value = value * 10 + (digit - '0');
    }
    total += value;
    if (!ts.skip(token_kind::op, "+")) {
      return total;
    }
  }
}

// template := '"' ( raw | '${' sum '}' )* '"'
rendered parse_template(token_stream &ts) {
  if (auto open = ts.expect(token_kind::punctuation, "\"", "expected a string"); !open) {
    return reloco::unexpected<parse_error>(open.error());
  }
  std::string text;
  for (;;) {
    // Raw text: everything up to the closing quote or the start of an interpolation, spaces included.
    const microfmt::token raw = ts.take_raw_until("\"$");
    text.append(raw.text.data(), raw.text.size());
    if (ts.skip(token_kind::punctuation, "\"")) {
      return text;
    }
    if (auto dollar = ts.expect(token_kind::punctuation, "$", "unterminated string"); !dollar) {
      return reloco::unexpected<parse_error>(dollar.error());
    }
    if (auto open = ts.expect(token_kind::punctuation, "{", "expected '{' after '$'"); !open) {
      return reloco::unexpected<parse_error>(open.error());
    }
    // Back in code: numbers and `+` are lexed normally between `${` and `}`.
    auto value = parse_sum(ts);
    if (!value) {
      return reloco::unexpected<parse_error>(value.error());
    }
    if (auto close = ts.expect(token_kind::punctuation, "}", "expected '}'"); !close) {
      return reloco::unexpected<parse_error>(close.error());
    }
    text += std::to_string(value.value());
  }
}

} // namespace

int main() {
  constexpr string_view inputs[] = {"\"total: ${1 + 2} items, ${40 + 2}!\"", "\"broken ${1 +\n  }\""};

  // Outside the string: `"` is an ordinary quote. Inside: it is plain punctuation, the parser drives the string.
  const microfmt::lexer_rules code_rules;
  microfmt::lexer_rules string_rules;
  string_rules.quotes = "";
  string_rules.punctuation = "\"${}";
  string_rules.operators = kPlus;

  microfmt::buffer_sink<1024> out;
  for (const string_view input : inputs) {
    token_stream ts{input, code_rules};
    const rendered result = microfmt::with_rules(ts, string_rules, [](token_stream &s) { return parse_template(s); });
    if (result) {
      microfmt::format_to(out.as_sink(), "{} => {}\n", input,
                          string_view{result.value().data(), result.value().size()});
    } else {
      microfmt::format_to(out.as_sink(), "{}\n", microfmt::report(result.error(), input));
    }
  }

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  std::printf("%.*s\n", static_cast<int>(out.view().size()), out.view().data());

  RELOCO_END_UNSAFE_BUFFER_USAGE;
}
