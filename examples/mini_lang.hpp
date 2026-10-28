// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

// A tiny scripting language that uses every token_stream helper in one place. See docs/mini-language.md
// for the walkthrough. Each expression is parsed into a small tree stored in a node_pool, evaluated, and the pool is
// rolled back; statements run one by one.
//
//   program    := statement*
//   statement  := let | print | block | fixity
//   let        := "let" NAME [ "=" expression ] ";"
//   print      := "print" "(" expression ")" ";"
//   block      := "{" statement* "}"
//   fixity     := ("infixl" | "infixr" | "infix") NUMBER STRING "=" ("min" | "max") ";"
//   expression := numbers, variables, calls NAME "(" [expression {"," expression}] ")",
//                 unary -, binary + - * / > <, "?:", "if" expression "then" expression "else" expression
//                 and parentheses

#include <cstddef>
#include <cstdint>

#include <microfmt/microfmt.hpp>
#include <microfmt/parsing/ast_pool.hpp>
#include <microfmt/parsing/number_parse.hpp>
#include <microfmt/parsing/operator_table.hpp>
#include <microfmt/parsing/string_pool.hpp>
#include <microfmt/parsing/token_stream_formatters.hpp>

namespace mini_lang {

using microfmt::bracket_pair;
using microfmt::node_id;
using microfmt::parse_error;
using microfmt::string_view;
using microfmt::token;
using microfmt::token_kind;
using microfmt::token_stream;

using value_result = reloco::expected<int64_t, parse_error>;
using status = reloco::expected<void, parse_error>;

// Operators live in an operator_table so a program can add its own (see parse_fixity). Precedences are only
// relative: comparison binds looser than arithmetic, and the ternary (precedence 5) looser than all of them.
inline constexpr size_t kMaxOperators = 16;
inline constexpr size_t kMaxUserOperators = 8;
inline constexpr string_view kBuiltinOperators = "+-*/<>";

// After a broken statement, resume at the next ';' that is not inside a `{ ... }` block.
inline constexpr microfmt::token_sync kStatementEnd[] = {{token_kind::punctuation, ";"}};
inline constexpr bracket_pair kParens[] = {{token_kind::punctuation, "(", token_kind::punctuation, ")"}};
inline constexpr bracket_pair kBraces[] = {{token_kind::punctuation, "{", token_kind::punctuation, "}"}};

inline constexpr size_t kMaxVariables = 16;
inline constexpr size_t kMaxArguments = 8;
inline constexpr size_t kMaxDepth = 32;
// Nodes of one expression; more is an error ("node pool full"). The first 32 live inside the interpreter.
inline constexpr size_t kMaxNodes = 128;

enum class node_kind : uint8_t { number, variable, binary, user_binary, negate, ternary, call };
enum class function_id : uint8_t { abs, max, min };

// One syntax-tree node. Children are handles into the interpreter's node_pool, not pointers. `at` is the token
// errors are reported at (the literal, the variable, the divisor, the callee).
struct RELOCO_POINTER node {
  node_kind kind{node_kind::number};
  char op{0};                                         // binary: one of + - * / > <
  function_id function{function_id::abs};             // call
  size_t slot{0};                                     // variable: index in the variable table
  int64_t value{0};                                   // number
  token at{};
  node_id<node> a{};                                  // operand / condition
  node_id<node> b{};                                  // right operand / then branch
  node_id<node> c{};                                  // else branch
  reloco::array<node_id<node>, kMaxArguments> args{}; // call arguments
  size_t arg_count{0};
};

using node_result = reloco::expected<node_id<node>, parse_error>;

namespace detail {

// Two's-complement wrap-around keeps hostile input from triggering signed-overflow UB.
constexpr int64_t wrap(uint64_t v) noexcept { return static_cast<int64_t>(v); }

} // namespace detail

/**
 * @brief Runs a program: parses, evaluates, collects every error and the `print` output.
 *
 * Borrows `source` (error positions point into it), so `source` must outlive the interpreter. Variable names are
 * interned in a `string_pool`, so they do not depend on the source buffer.
 */
class RELOCO_POINTER interpreter {
public:
  explicit interpreter(string_view source RELOCO_LIFETIMEBOUND) noexcept : source_{source} { declare_builtin_operators(); }

  /** @return Number of statements that ran without error. */
  [[nodiscard]] size_t run() noexcept {
    // "=" is the assignment in `let`; the table supplies every other operator spelling to the lexer.
    rules_.operators = operator_spellings();
    token_stream ts{source_, rules_};
    auto statement = [this](token_stream &s) -> status { return parse_statement(s); };
    return microfmt::parse_statements(ts, kStatementEnd, kBraces, errors_, statement);
  }

  [[nodiscard]] string_view output() const noexcept RELOCO_LIFETIMEBOUND { return out_.view(); }
  [[nodiscard]] const microfmt::error_list<16> &errors() const noexcept RELOCO_LIFETIMEBOUND { return errors_; }
  [[nodiscard]] string_view source() const noexcept { return source_; }

private:
  struct RELOCO_COPYABLE variable {
    microfmt::string_id name{}; // interned: comparing two names is comparing two integers
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
        [this](token_stream &s) -> status { return parse_block(s); },
        [this](token_stream &s) -> status { return parse_fixity(s); });
  }

  // `infixl 20 "<>" = min;` declares a binary operator at run time: its precedence (relative to the others), its
  // associativity (`infixl`, `infixr`, or `infix` which forbids chaining) and the function it applies.
  status parse_fixity(token_stream &ts) noexcept {
    const token keyword = ts.peek();
    const bool left = keyword.text == "infixl";
    const bool right = keyword.text == "infixr";
    if (keyword.kind != token_kind::identifier || !(left || right || keyword.text == "infix")) {
      return reloco::unexpected<parse_error>(ts.error_here("expected a statement: let, print or a block"));
    }
    ts.next();
    auto level = ts.expect(token_kind::integer, "expected a precedence number");
    auto spelling = level ? ts.expect(token_kind::string, "expected the operator in quotes")
                          : reloco::expected<token, parse_error>(reloco::unexpected<parse_error>(level.error()));
    if (!spelling) {
      return reloco::unexpected<parse_error>(spelling.error());
    }
    if (auto eq = ts.expect(token_kind::op, "=", "expected '='"); !eq) {
      return reloco::unexpected<parse_error>(eq.error());
    }
    auto function = ts.expect(token_kind::identifier, "expected min or max");
    if (!function) {
      return reloco::unexpected<parse_error>(function.error());
    }
    if (function.value().text != "min" && function.value().text != "max") {
      return reloco::unexpected<parse_error>(parse_error{"expected min or max", function.value()});
    }
    if (auto semi = ts.expect(token_kind::punctuation, ";", "expected ';'"); !semi) {
      return reloco::unexpected<parse_error>(semi.error());
    }
    auto number = microfmt::parse<uint8_t>(level.value().text);
    if (!number) {
      return reloco::unexpected<parse_error>(parse_error{"precedence out of range", level.value()});
    }
    const string_view quoted = spelling.value().text;
    const string_view text = quoted.size() >= 2 ? quoted.substr(1, quoted.size() - 2) : string_view{};
    if (user_operator_count_ == user_operators_.size()) {
      return reloco::unexpected<parse_error>(parse_error{"too many operators", spelling.value()});
    }
    auto name = operator_names_.intern(text, spelling.value());
    if (!name) {
      return reloco::unexpected<parse_error>(name.error());
    }
    const microfmt::assoc how = left ? microfmt::assoc::left : (right ? microfmt::assoc::right : microfmt::assoc::none);
    if (auto declared = operators_.add_binary(text, number.value(), how); !declared) {
      return reloco::unexpected<parse_error>(parse_error{declared.error(), spelling.value()});
    }
    user_operators_[user_operator_count_++] =
        user_operator{name.value(), function.value().text == "max" ? function_id::max : function_id::min};
    // The lexer must now emit the new spelling as an `op` token: swap the rules and re-tokenize the lookahead.
    rules_.operators = operator_spellings();
    ts.set_rules(rules_);
    return {};
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

  // Builds nodes: parse_expression looks these callbacks up by name. Nothing is evaluated here.
  struct RELOCO_POINTER expression_actions {
    interpreter *self;

    node_result primary(token_stream &s) const noexcept { return self->parse_primary(s); }

    node_result make_binary(const microfmt::binary_op &op, node_id<node> l, node_id<node> r) const noexcept {
      node n;
      n.kind = node_kind::binary;
      n.op = op.text.size() == 1 ? op.text[0] : '\0';
      if (n.op == '\0' || kBuiltinOperators.find(n.op) == string_view::npos) {
        n.kind = node_kind::user_binary;
        n.function = self->function_of(op.text);
      }
      n.a = l;
      n.b = r;
      // A division by zero is reported at the divisor.
      n.at = self->pool_.get(r)->at;
      return self->pool_.make(n.at, n);
    }

    node_result make_prefix(const microfmt::prefix_op &, node_id<node> v) const noexcept {
      node n;
      n.kind = node_kind::negate;
      n.a = v;
      n.at = self->pool_.get(v)->at;
      return self->pool_.make(n.at, n);
    }

    node_result make_ternary(const microfmt::ternary_op &, node_id<node> c, node_id<node> t,
                             node_id<node> e) const noexcept {
      return self->make_ternary_node(c, t, e);
    }

    // `if cond then a else b`: the helper consumed "if"; we parse the three parts by re-entering the
    // expression parser (expr.parse) and consume `then` / `else` ourselves. The else branch is parsed at the
    // loosest precedence (0), so `if a then b else c + 1` is `if ... else (c + 1)`.
    template <typename Expr>
    node_result make_mixfix(const microfmt::mixfix_op &, token_stream &s, Expr &expr) const noexcept {
      auto cond = expr.parse(0);
      if (!cond) {
        return cond;
      }
      if (auto kw = s.expect(token_kind::identifier, "then", "expected 'then'"); !kw) {
        return reloco::unexpected<parse_error>(kw.error());
      }
      auto then_node = expr.parse(0);
      if (!then_node) {
        return then_node;
      }
      if (auto kw = s.expect(token_kind::identifier, "else", "expected 'else'"); !kw) {
        return reloco::unexpected<parse_error>(kw.error());
      }
      auto else_node = expr.parse(0);
      if (!else_node) {
        return else_node;
      }
      return self->make_ternary_node(cond.value(), then_node.value(), else_node.value());
    }
  };

  node_result make_ternary_node(node_id<node> c, node_id<node> t, node_id<node> e) noexcept {
    node n;
    n.kind = node_kind::ternary;
    n.a = c;
    n.b = t;
    n.c = e;
    n.at = pool_.get(c)->at;
    return pool_.make(n.at, n);
  }

  // Parses one expression into the pool and returns its root. Names are resolved here, so an unknown variable
  // or function is reported while parsing (and, in a call, together with the other bad arguments).
  node_result parse_tree(token_stream &ts) noexcept {
    expression_actions actions{this};
    return microfmt::parse_expression<node_id<node>>(ts, operators_.grammar(), actions);
  }

  // Parse, evaluate, then give the nodes back: the tree is only needed for the one statement that uses it.
  value_result expression(token_stream &ts) noexcept {
    const microfmt::pool_mark before = pool_.mark();
    auto tree = parse_tree(ts);
    if (!tree) {
      pool_.rollback(before);
      return reloco::unexpected<parse_error>(tree.error());
    }
    auto value = evaluate(tree.value());
    pool_.rollback(before);
    return value;
  }

  // The leaves of an expression: a number, a variable or a call. "(" expression ")" never reaches here.
  node_result parse_primary(token_stream &ts) noexcept {
    constexpr token_kind kKinds[] = {token_kind::integer, token_kind::identifier};
    auto tok = microfmt::expect_one_of(ts, kKinds, "expected a number, variable or call");
    if (!tok) {
      return reloco::unexpected<parse_error>(tok.error());
    }
    node n;
    n.at = tok.value();
    if (tok.value().kind == token_kind::integer) {
      auto number = microfmt::parse<int64_t>(tok.value().text);
      if (!number) {
        return reloco::unexpected<parse_error>(parse_error{"number out of range", tok.value()});
      }
      n.kind = node_kind::number;
      n.value = number.value();
      return pool_.make(n.at, n);
    }
    if (ts.check(token_kind::punctuation, "(")) {
      return parse_call(ts, tok.value());
    }
    // find() never inserts, so a mistyped name does not grow the pool; an invalid id matches no variable.
    const microfmt::string_id wanted = strings_.find(tok.value().text);
    for (size_t i = 0; i < variable_count_; ++i) {
      if (variables_[i].name == wanted) {
        n.kind = node_kind::variable;
        n.slot = i;
        return pool_.make(n.at, n);
      }
    }
    return reloco::unexpected<parse_error>(parse_error{"unknown variable", tok.value()});
  }

  // NAME "(" [expression {"," expression}] ")": parse_list_recovering handles the brackets, the commas
  // and skipping a bad argument.
  node_result parse_call(token_stream &ts, const token &name) noexcept {
    microfmt::depth_guard guard{depth_, kMaxDepth};
    if (!guard) {
      return reloco::unexpected<parse_error>(ts.error_here("nesting too deep"));
    }
    node call;
    call.kind = node_kind::call;
    call.at = name;
    auto item = [this, &call](token_stream &s) -> status {
      auto arg = parse_tree(s);
      if (!arg) {
        return reloco::unexpected<parse_error>(arg.error());
      }
      if (call.arg_count == call.args.size()) {
        return reloco::unexpected<parse_error>(s.error_here("too many arguments"));
      }
      call.args[call.arg_count++] = arg.value();
      return {};
    };
    const microfmt::list_options options{"(", ")", ",", false};
    // Each bad argument is recorded in errors_ and skipped, so `max(1, +, *)` reports both mistakes.
    auto failed = microfmt::parse_list_recovering(ts, options, errors_, kParens, item);
    if (!failed) {
      return reloco::unexpected<parse_error>(failed.error());
    }
    if (failed.value() != 0) {
      // The individual errors are already listed; this one marks the call itself as unusable.
      return reloco::unexpected<parse_error>(parse_error{"call has invalid arguments", name});
    }
    const bool is_abs = name.text == "abs";
    const bool is_max = name.text == "max";
    const bool is_min = name.text == "min";
    if (!(is_abs || is_max || is_min)) {
      return reloco::unexpected<parse_error>(parse_error{"unknown function", name});
    }
    if (call.arg_count == 0 || (is_abs && call.arg_count != 1)) {
      return reloco::unexpected<parse_error>(parse_error{"wrong number of arguments", name});
    }
    call.function = is_abs ? function_id::abs : (is_max ? function_id::max : function_id::min);
    return pool_.make(name, call);
  }

  // Walks the tree. The pool is not changed while this runs, so the borrowed node stays valid.
  value_result evaluate(node_id<node> id) noexcept {
    const auto ref = pool_.get(id);
    const node &n = *ref;
    switch (n.kind) {
    case node_kind::number:
      return n.value;
    case node_kind::variable:
      return variables_[n.slot].value;
    case node_kind::negate: {
      auto v = evaluate(n.a);
      if (!v) {
        return v;
      }
      return detail::wrap(0 - static_cast<uint64_t>(v.value()));
    }
    case node_kind::binary: {
      auto l = evaluate(n.a);
      if (!l) {
        return l;
      }
      auto r = evaluate(n.b);
      if (!r) {
        return r;
      }
      return apply_binary(n, l.value(), r.value());
    }
    case node_kind::user_binary: {
      auto l = evaluate(n.a);
      if (!l) {
        return l;
      }
      auto r = evaluate(n.b);
      if (!r) {
        return r;
      }
      const reloco::array<int64_t, kMaxArguments> pair{l.value(), r.value()};
      return apply_function(n.function, pair, 2);
    }
    case node_kind::ternary: {
      // Only the chosen branch runs, which is the point of having a tree.
      auto cond = evaluate(n.a);
      if (!cond) {
        return cond;
      }
      return evaluate(cond.value() != 0 ? n.b : n.c);
    }
    case node_kind::call: {
      reloco::array<int64_t, kMaxArguments> values{};
      for (size_t i = 0; i < n.arg_count; ++i) {
        auto v = evaluate(n.args[i]);
        if (!v) {
          return v;
        }
        values[i] = v.value();
      }
      return apply_function(n.function, values, n.arg_count);
    }
    }
    return 0;
  }

  static value_result apply_binary(const node &n, int64_t l, int64_t r) noexcept {
    const auto a = static_cast<uint64_t>(l);
    const auto b = static_cast<uint64_t>(r);
    switch (n.op) {
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
        return reloco::unexpected<parse_error>(parse_error{"division by zero", n.at});
      }
      return r == -1 ? detail::wrap(0 - a) : l / r; // INT64_MIN / -1 would overflow
    }
  }

  static int64_t apply_function(function_id fn, const reloco::array<int64_t, kMaxArguments> &args,
                                size_t count) noexcept {
    if (fn == function_id::abs) {
      return args[0] < 0 ? detail::wrap(0 - static_cast<uint64_t>(args[0])) : args[0];
    }
    int64_t best = args[0];
    for (size_t i = 1; i < count; ++i) {
      best = fn == function_id::max ? (args[i] > best ? args[i] : best) : (args[i] < best ? args[i] : best);
    }
    return best;
  }

  // The built-in operators, declared with numeric precedences (which only fix their order).
  void declare_builtin_operators() noexcept {
    (void)operators_.add_binary("+", 10);
    (void)operators_.add_binary("-", 10);
    (void)operators_.add_binary("*", 20);
    (void)operators_.add_binary("/", 20);
    (void)operators_.add_binary(">", 8);
    (void)operators_.add_binary("<", 8);
    (void)operators_.add_prefix("-", 30);
    (void)operators_.add_mixfix("if", 5);
    (void)operators_.set_ternary(microfmt::ternary_op{});
  }

  // "=" and ":" are not operators of the table but `let` and `?:` need the lexer to emit them as `op`.
  [[nodiscard]] microfmt::span<const string_view> operator_spellings() noexcept {
    extra_spellings_.clear();
    (void)extra_spellings_.try_push_back("=");
    (void)extra_spellings_.try_push_back("?");
    (void)extra_spellings_.try_push_back(":");
    const auto declared = operators_.spellings();
    for (size_t i = 0; i < declared.size(); ++i) {
      (void)extra_spellings_.try_push_back(declared[i]);
    }
    return microfmt::detail::view_of<string_view>(extra_spellings_);
  }

  function_id function_of(string_view spelling) const noexcept {
    const microfmt::string_id wanted = operator_names_.find(spelling);
    for (size_t i = 0; i < user_operator_count_; ++i) {
      if (user_operators_[i].name == wanted) {
        return user_operators_[i].function;
      }
    }
    return function_id::max;
  }

  status define(const token &name, int64_t value) noexcept {
    const microfmt::string_id known = strings_.find(name.text);
    for (size_t i = 0; i < variable_count_; ++i) {
      if (variables_[i].name == known) {
        variables_[i].value = value;
        return {};
      }
    }
    if (variable_count_ == variables_.size()) {
      return reloco::unexpected<parse_error>(parse_error{"too many variables", name});
    }
    auto id = strings_.intern(name);
    if (!id) {
      return reloco::unexpected<parse_error>(id.error());
    }
    variables_[variable_count_++] = variable{id.value(), value};
    return {};
  }

  struct RELOCO_COPYABLE user_operator {
    microfmt::string_id name{};
    function_id function{function_id::max};
  };

  string_view source_;
  microfmt::lexer_rules rules_{};
  microfmt::operator_table<kMaxOperators> operators_{kMaxOperators};
  microfmt::string_pool<64> operator_names_{256, kMaxUserOperators};
  reloco::array<user_operator, kMaxUserOperators> user_operators_{};
  size_t user_operator_count_{0};
  reloco::sso_vector<string_view, kMaxOperators + 4> extra_spellings_{};
  reloco::array<variable, kMaxVariables> variables_{};
  size_t variable_count_{0};
  size_t depth_{0};
  microfmt::error_list<16> errors_{};
  microfmt::node_pool<node, 32> pool_{kMaxNodes};
  microfmt::string_pool<256> strings_{1024, kMaxVariables};
  microfmt::buffer_sink<1024> out_{};
};

} // namespace mini_lang
