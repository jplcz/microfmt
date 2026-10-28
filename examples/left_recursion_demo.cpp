// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Left recursion without a precedence table: `access := name ( "." name | "[" number "]" | "(" number ")" )*`.
// parse_left_recursive parses the seed (`config`) and keeps extending it while a suffix matches.
// The value is a small text description of each step so the left-to-right order is visible.

#include <cstdio>
#include <string>

#include <microfmt/microfmt.hpp>
#include <microfmt/parsing/token_stream_formatters.hpp>

namespace {

using microfmt::parse_error;
using microfmt::string_view;
using microfmt::token_kind;
using microfmt::token_stream;

using description = reloco::expected<std::string, parse_error>;

std::string to_string(string_view text) { return std::string{text.data(), text.size()}; }

description parse_access(token_stream &ts) {
  // The seed: the leftmost operand of the rule.
  auto seed = [](token_stream &s) -> description {
    auto name = s.expect(token_kind::identifier, "expected a name");
    if (!name) {
      return reloco::unexpected<parse_error>(name.error());
    }
    return to_string(name.value().text);
  };

  // One suffix step. `left` is what was parsed so far; replace it with the extended value and return true,
  // or return false (consuming nothing) when no suffix starts at the next token.
  auto extend = [](token_stream &s, std::string &left) -> reloco::expected<bool, parse_error> {
    if (s.skip(token_kind::punctuation, ".")) {
      auto member = s.expect(token_kind::identifier, "expected a member name");
      if (!member) {
        return reloco::unexpected<parse_error>(member.error());
      }
      left = "member(" + left + ", " + to_string(member.value().text) + ")";
      return true;
    }
    // "[" and "(" suffixes share the same shape: an integer followed by the matching closer.
    for (const string_view open : {string_view{"["}, string_view{"("}}) {
      if (!s.skip(token_kind::punctuation, open)) {
        continue;
      }
      const string_view close = open == "[" ? "]" : ")";
      auto number = s.expect(token_kind::integer, "expected a number");
      if (!number) {
        return reloco::unexpected<parse_error>(number.error());
      }
      if (auto closer = s.expect(token_kind::punctuation, close, "expected the closing bracket"); !closer) {
        return reloco::unexpected<parse_error>(closer.error());
      }
      left = std::string{open == "[" ? "index(" : "call("} + left + ", " + to_string(number.value().text) + ")";
      return true;
    }
    return false;
  };

  // 8 limits the number of suffixes so a hostile `a.a.a.a...` cannot run forever.
  return microfmt::parse_left_recursive<std::string>(ts, seed, extend, 8);
}

} // namespace

int main() {
  constexpr string_view inputs[] = {"config.servers[1].host(8080)", "config.servers[", "config."};

  microfmt::lexer_rules rules;
  rules.punctuation = ".[]()";

  microfmt::buffer_sink<1024> out;
  for (const string_view input : inputs) {
    token_stream ts{input, rules};
    const description result = parse_access(ts);
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
