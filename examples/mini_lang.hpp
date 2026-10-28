// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

// A tiny scripting language that uses every token_stream helper in one place. See docs/mini-language.md
// for the walkthrough. The grammar is parsed and evaluated in a single pass (no syntax tree).
//
//   program    := statement*
//   statement  := let | print | block
//   let        := "let" NAME [ "=" expression ] ";"
//   print      := "print" "(" expression ")" ";"
//   block      := "{" statement* "}"
//   expression := numbers, variables, calls NAME "(" [expression {"," expression}] ")",
//                 unary -, binary + - * / > <, "?:", "if" expression "then" expression "else" expression
//                 and parentheses

#include <cstddef>
#include <cstdint>

#include <microfmt/microfmt.hpp>
#include <microfmt/parsing/number_parse.hpp>
#include <microfmt/parsing/token_stream_formatters.hpp>

namespace mini_lang {

using microfmt::bracket_pair;
using microfmt::parse_error;
using microfmt::string_view;
using microfmt::token;
using microfmt::token_kind;
using microfmt::token_stream;

using value_result = reloco::expected<int64_t, parse_error>;
using status = reloco::expected<void, parse_error>;

// The lexer must emit these as `op` tokens; "=" is the assignment in `let`.
inline constexpr string_view kOperators[] = {"+", "-", "*", "/", ">", "<", "=", "?", ":"};

// Comparison binds looser than arithmetic, and the ternary (precedence 5) looser than comparison.
inline constexpr microfmt::binary_op kBinary[] = {{"+", 10}, {"-", 10}, {"*", 20}, {"/", 20}, {">", 8}, {"<", 8}};
inline constexpr microfmt::prefix_op kPrefix[] = {{"-", 30}};
// `if` introduces a keyword form; its else branch binds as loosely as the ternary.
inline constexpr microfmt::mixfix_op kMixfix[] = {{"if", 5}};

// After a broken statement, resume at the next ';' that is not inside a `{ ... }` block.
inline constexpr microfmt::token_sync kStatementEnd[] = {{token_kind::punctuation, ";"}};
inline constexpr bracket_pair kBraces[] = {{token_kind::punctuation, "{", token_kind::punctuation, "}"}};

inline constexpr size_t kMaxVariables = 16;
inline constexpr size_t kMaxArguments = 8;
inline constexpr size_t kMaxDepth = 32;

namespace detail {

// Two's-complement wrap-around keeps hostile input from triggering signed-overflow UB.
constexpr int64_t wrap(uint64_t v) noexcept { return static_cast<int64_t>(v); }

} // namespace detail

/**
 * @brief Runs a program: parses, evaluates, collects every error and the `print` output.
 *
 * Borrows `source` (errors and variable names point into it), so `source` must outlive the interpreter.
 */
class RELOCO_POINTER interpreter {
public:
  explicit interpreter(string_view source RELOCO_LIFETIMEBOUND) noexcept : source_{source} {}

  /** @return Number of statements that ran without error. */
  [[nodiscard]] size_t run() noexcept {
    microfmt::lexer_rules rules;
    rules.operators = kOperators;
    token_stream ts{source_, rules};
    auto statement = [this](token_stream &s) -> status { return parse_statement(s); };
    return microfmt::parse_statements(ts, kStatementEnd, kBraces, errors_, statement);
  }

  [[nodiscard]] string_view output() const noexcept RELOCO_LIFETIMEBOUND { return out_.view(); }
  [[nodiscard]] const microfmt::error_list<16> &errors() const noexcept RELOCO_LIFETIMEBOUND { return errors_; }
  [[nodiscard]] string_view source() const noexcept { return source_; }

private:
  struct RELOCO_POINTER variable {
    string_view name{};
    int64_t value{0};
  };

  // statement := let | print | block. parse_choice tries each from the same spot and reports the error of
  // whichever got furthest, so `let x 5;` says "expected ';'" rather than "expected a statement".
  status parse_statement(token_stream &ts) noexcept {
    microfmt::depth_guard guard{depth_, kMaxDepth};
    if (!guard) {
      return reloco::unexpected<parse_error>(ts.error_here("nesting too deep"));
    }
    return microfmt::parse_choice(
        ts, [this](token_stream &s) -> status { return parse_let(s); },
        [this](token_stream &s) -> status { return parse_print(s); },
        [this](token_stream &s) -> status { return parse_block(s); });
  }

  status parse_let(token_stream &ts) noexcept {
    if (!ts.skip(token_kind::identifier, "let")) {
      return reloco::unexpected<parse_error>(ts.error_here("expected a statement: let, print or a block"));
    }
    auto name = ts.expect(token_kind::identifier, "expected a variable name");
    if (!name) {
      return reloco::unexpected<parse_error>(name.error());
    }
    // `let x;` is allowed: parse_optional yields "nothing" only when no '=' follows. Once '=' is
    // consumed, a bad expression is a real error rather than "no initialiser".
    auto init = microfmt::parse_optional(ts, [this](token_stream &s) -> value_result {
      if (auto eq = s.expect(token_kind::op, "=", "expected '='"); !eq) {
        return reloco::unexpected<parse_error>(eq.error());
      }
      return expression(s);
    });
    if (!init) {
      return reloco::unexpected<parse_error>(init.error());
    }
    if (auto semi = ts.expect(token_kind::punctuation, ";", "expected ';'"); !semi) {
      return reloco::unexpected<parse_error>(semi.error());
    }
    return define(name.value(), init.value().has_value() ? *init.value() : 0);
  }

  status parse_print(token_stream &ts) noexcept {
    if (!ts.skip(token_kind::identifier, "print")) {
      return reloco::unexpected<parse_error>(ts.error_here("expected a statement: let, print or a block"));
    }
    if (auto open = ts.expect(token_kind::punctuation, "(", "expected '('"); !open) {
      return reloco::unexpected<parse_error>(open.error());
    }
    auto value = expression(ts);
    if (!value) {
      return reloco::unexpected<parse_error>(value.error());
    }
    if (auto close = ts.expect(token_kind::punctuation, ")", "expected ')'"); !close) {
      return reloco::unexpected<parse_error>(close.error());
    }
    if (auto semi = ts.expect(token_kind::punctuation, ";", "expected ';'"); !semi) {
      return reloco::unexpected<parse_error>(semi.error());
    }
    microfmt::format_to(out_.as_sink(), "{}\n", value.value());
    return {};
  }

  // The block parses its own body with recovery, so an error inside it never makes the outer loop
  // resume mid-block. Body errors are recorded in `errors_` and the block still counts as parsed.
  status parse_block(token_stream &ts) noexcept {
    auto open = ts.expect(token_kind::punctuation, "{", "expected a statement: let, print or a block");
    if (!open) {
      return reloco::unexpected<parse_error>(open.error());
    }
    auto statement = [this](token_stream &s) -> status { return parse_statement(s); };
    (void)microfmt::parse_statements_until(ts, token_kind::punctuation, "}", kStatementEnd, kBraces, errors_,
                                           statement);
    if (auto close = ts.expect(token_kind::punctuation, "}", "expected '}'"); !close) {
      // The missing '}' is only noticed at the end of the input; the note points back at the opener.
      return reloco::unexpected<parse_error>(microfmt::with_note(close.error(), "block opened here", open.value()));
    }
    return {};
  }

  // The callbacks that turn parsed pieces into values; parse_expression looks them up by name.
  struct RELOCO_POINTER expression_actions {
    interpreter *self;
    token_stream *stream;

    value_result primary(token_stream &s) const noexcept { return self->parse_primary(s); }

    value_result make_binary(const microfmt::binary_op &op, int64_t l, int64_t r) const noexcept {
      const auto a = static_cast<uint64_t>(l);
      const auto b = static_cast<uint64_t>(r);
      switch (op.text[0]) {
      case '+':
        return detail::wrap(a + b);
      case '-':
        return detail::wrap(a - b);
      case '*':
        return detail::wrap(a * b);
      case '>':
        return l > r ? 1 : 0;
      case '<':
        return l < r ? 1 : 0;
      default:
        if (r == 0) {
          return reloco::unexpected<parse_error>(stream->error_here("division by zero"));
        }
        return r == -1 ? detail::wrap(0 - a) : l / r; // INT64_MIN / -1 would overflow
      }
    }

    value_result make_prefix(const microfmt::prefix_op &, int64_t v) const noexcept {
      return detail::wrap(0 - static_cast<uint64_t>(v));
    }

    // Both branches of `?:` are already evaluated; that is fine because expressions have no side effects.
    value_result make_ternary(const microfmt::ternary_op &, int64_t c, int64_t t, int64_t e) const noexcept {
      return c != 0 ? t : e;
    }

    // `if cond then a else b`: the helper consumed "if"; we parse the three parts by re-entering the
    // expression parser (expr.parse) and consume `then` / `else` ourselves. The else branch is parsed at the
    // mixfix precedence, so `if a then b else c + 1` is `if ... else (c + 1)`.
    template <typename Expr>
    value_result make_mixfix(const microfmt::mixfix_op &op, token_stream &s, Expr &expr) const noexcept {
      auto cond = expr.parse(0);
      if (!cond) {
        return cond;
      }
      if (auto kw = s.expect(token_kind::identifier, "then", "expected 'then'"); !kw) {
        return reloco::unexpected<parse_error>(kw.error());
      }
      auto then_value = expr.parse(0);
      if (!then_value) {
        return then_value;
      }
      if (auto kw = s.expect(token_kind::identifier, "else", "expected 'else'"); !kw) {
        return reloco::unexpected<parse_error>(kw.error());
      }
      auto else_value = expr.parse(op.precedence);
      if (!else_value) {
        return else_value;
      }
      return cond.value() != 0 ? then_value.value() : else_value.value();
    }
  };

  value_result expression(token_stream &ts) noexcept {
    expression_actions actions{this, &ts};
    const microfmt::expression_grammar grammar{kBinary, kPrefix, {}, kMixfix, microfmt::ternary_op{}};
    return microfmt::parse_expression<int64_t>(ts, grammar, actions);
  }

  // The leaves of an expression: a number, a variable or a call. "(" expression ")" never reaches here.
  value_result parse_primary(token_stream &ts) noexcept {
    constexpr token_kind kKinds[] = {token_kind::integer, token_kind::identifier};
    auto tok = microfmt::expect_one_of(ts, kKinds, "expected a number, variable or call");
    if (!tok) {
      return reloco::unexpected<parse_error>(tok.error());
    }
    if (tok.value().kind == token_kind::integer) {
      auto number = microfmt::parse<int64_t>(tok.value().text);
      if (!number) {
        return reloco::unexpected<parse_error>(parse_error{"number out of range", tok.value()});
      }
      return number.value();
    }
    if (ts.check(token_kind::punctuation, "(")) {
      return parse_call(ts, tok.value());
    }
    for (size_t i = 0; i < variable_count_; ++i) {
      if (variables_[i].name == tok.value().text) {
        return variables_[i].value;
      }
    }
    return reloco::unexpected<parse_error>(parse_error{"unknown variable", tok.value()});
  }

  // NAME "(" [expression {"," expression}] ")": parse_list handles the brackets and commas.
  value_result parse_call(token_stream &ts, const token &name) noexcept {
    microfmt::depth_guard guard{depth_, kMaxDepth};
    if (!guard) {
      return reloco::unexpected<parse_error>(ts.error_here("nesting too deep"));
    }
    reloco::array<int64_t, kMaxArguments> args{};
    size_t count = 0;
    auto item = [this, &args, &count](token_stream &s) -> status {
      auto value = expression(s);
      if (!value) {
        return reloco::unexpected<parse_error>(value.error());
      }
      if (count == args.size()) {
        return reloco::unexpected<parse_error>(s.error_here("too many arguments"));
      }
      args[count++] = value.value();
      return {};
    };
    const microfmt::list_options options{"(", ")", ",", false};
    if (auto list = microfmt::parse_list(ts, options, item); !list) {
      return reloco::unexpected<parse_error>(list.error());
    }
    return apply(name, args, count);
  }

  static value_result apply(const token &name, const reloco::array<int64_t, kMaxArguments> &args,
                            size_t count) noexcept {
    if (name.text == "abs" && count == 1) {
      return args[0] < 0 ? detail::wrap(0 - static_cast<uint64_t>(args[0])) : args[0];
    }
    if ((name.text == "max" || name.text == "min") && count >= 1) {
      int64_t best = args[0];
      for (size_t i = 1; i < count; ++i) {
        best = name.text == "max" ? (args[i] > best ? args[i] : best) : (args[i] < best ? args[i] : best);
      }
      return best;
    }
    const bool known = name.text == "abs" || name.text == "max" || name.text == "min";
    return reloco::unexpected<parse_error>(parse_error{known ? "wrong number of arguments" : "unknown function", name});
  }

  status define(const token &name, int64_t value) noexcept {
    for (size_t i = 0; i < variable_count_; ++i) {
      if (variables_[i].name == name.text) {
        variables_[i].value = value;
        return {};
      }
    }
    if (variable_count_ == variables_.size()) {
      return reloco::unexpected<parse_error>(parse_error{"too many variables", name});
    }
    variables_[variable_count_++] = variable{name.text, value};
    return {};
  }

  string_view source_;
  reloco::array<variable, kMaxVariables> variables_{};
  size_t variable_count_{0};
  size_t depth_{0};
  microfmt::error_list<16> errors_{};
  microfmt::buffer_sink<1024> out_{};
};

} // namespace mini_lang
