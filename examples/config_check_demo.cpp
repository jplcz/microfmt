// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// A multi-error checker for `key = value;` scripts: parse_statements keeps going after each
// mistake and error_list collects them all, so the user sees every problem in one run.

#include <cstdio>

#include <microfmt/microfmt.hpp>
#include <microfmt/parsing/token_stream_formatters.hpp>

namespace {

using microfmt::token_kind;

constexpr microfmt::string_view kOperators[] = {"="};

// Where to resume after a broken statement: the next ';'.
constexpr microfmt::token_sync kStatementEnd[] = {{token_kind::punctuation, ";"}};

// Recovery treats `{ ... }` as one unit, so a ';' inside a block never ends it early.
constexpr microfmt::bracket_pair kBraces[] = {{token_kind::punctuation, "{", token_kind::punctuation, "}"}};

// setting := identifier '=' ( integer | string | identifier ) ';'
reloco::expected<void, microfmt::parse_error> setting(microfmt::token_stream &ts) {
  if (auto key = ts.expect(token_kind::identifier, "expected a setting name"); !key) {
    return reloco::unexpected<microfmt::parse_error>(key.error());
  }
  if (auto eq = ts.expect(token_kind::op, "=", "expected '='"); !eq) {
    return reloco::unexpected<microfmt::parse_error>(eq.error());
  }
  // A value is one of three token kinds; the message lists them all.
  constexpr token_kind kValueKinds[] = {token_kind::integer, token_kind::string, token_kind::identifier};
  if (auto value = microfmt::expect_one_of(ts, kValueKinds, "expected an integer, string or identifier"); !value) {
    return reloco::unexpected<microfmt::parse_error>(value.error());
  }
  if (auto semi = ts.expect(token_kind::punctuation, ";", "expected ';'"); !semi) {
    return reloco::unexpected<microfmt::parse_error>(semi.error());
  }
  return {};
}

// block := identifier '{' setting* '}'
reloco::expected<void, microfmt::parse_error> block(microfmt::token_stream &ts) {
  if (auto name = ts.expect(token_kind::identifier, "expected a block name"); !name) {
    return reloco::unexpected<microfmt::parse_error>(name.error());
  }
  if (auto open = ts.expect(token_kind::punctuation, "{", "expected '{'"); !open) {
    return reloco::unexpected<microfmt::parse_error>(open.error());
  }
  // parse_until needs an item that returns a value, so wrap `setting` and count them in the sink.
  auto one_setting = [](microfmt::token_stream &s) -> reloco::expected<int, microfmt::parse_error> {
    if (auto parsed = setting(s); !parsed) {
      return reloco::unexpected<microfmt::parse_error>(parsed.error());
    }
    return 1;
  };
  size_t settings = 0;
  if (auto body = microfmt::parse_until(ts, token_kind::punctuation, "}", one_setting, [&](int &&) { ++settings; });
      !body) {
    return reloco::unexpected<microfmt::parse_error>(body.error());
  }
  if (auto close = ts.expect(token_kind::punctuation, "}", "expected '}'"); !close) {
    return reloco::unexpected<microfmt::parse_error>(close.error());
  }
  return {};
}

// statement := setting | block. Setting goes first: when both fail at the same token the
// earlier alternative's message wins, and "expected '='" is the better hint for `retries 3;`.
reloco::expected<void, microfmt::parse_error> statement(microfmt::token_stream &ts) {
  return microfmt::parse_choice(ts, setting, block);
}

} // namespace

int main() {
  constexpr microfmt::string_view source = "name = \"demo\";\nretries 3;\nport = ;\ndebug = on;\nlevel = 2 2;\nnet { port = 80; host = local; }\nserver x { a = 1; b = 2; }\n";

  microfmt::lexer_rules rules;
  rules.operators = kOperators;
  microfmt::token_stream ts{source, rules};

  // Room for 8 errors; any more are only counted (overflowed() reports that).
  microfmt::error_list<8> errors;
  const size_t good = microfmt::parse_statements(ts, kStatementEnd, kBraces, errors, statement);

  microfmt::buffer_sink<1024> out;
  microfmt::format_to(out.as_sink(), "{} statements ok, {} errors\n", good, errors.total());
  const auto stored = errors.errors();
  for (size_t i = 0; i < stored.size(); ++i) {
    microfmt::format_to(out.as_sink(), "{}\n", microfmt::report(stored[i], source));
  }

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  std::printf("%.*s", static_cast<int>(out.view().size()), out.view().data());

  RELOCO_END_UNSAFE_BUFFER_USAGE;
}
