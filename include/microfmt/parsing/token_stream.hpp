// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

// std-interop-file: the std::string&& constructor overload is deleted to reject dangling temporaries.

/** @file token_stream.hpp
 * @brief Two-token lookahead stream over `lexer` for hand-written recursive-descent parsers.
 *
 * Why: every small parser repeats "is the next token X? consume it, else
 * report line/column". `token_stream` packages that, plus cheap backtracking
 * (copy the stream) and a recursion-depth guard. See `docs/token-stream.md`
 * for the user guide.
 */

#include "lexer.hpp"
#include <reloco/array.hpp>
#include <reloco/expected.hpp>
#include <reloco/optional.hpp>
#include <string>
#include <type_traits>
#include <utility>

namespace microfmt {

/** Why a parse failed. `message` must have static storage (use a string literal). */
struct RELOCO_POINTER parse_error {
  microfmt::string_view message{};
  token at{}; ///< Offending token; `at.line` / `at.column` locate it, `at.text` borrows the source.
  microfmt::string_view note{}; ///< Optional secondary message (static storage), empty for none.
  token note_at{};              ///< Where `note` points (for example the opening bracket); used when `note` is set.
};

/**
 * @brief Copy of `error` with a note attached, e.g. `with_note(err, "block opened here", open_brace)`.
 *
 * Why: some mistakes are detected far from their cause (a missing `}` is found at the end of input);
 * the note points at the cause. `note` must have static storage; one note per error.
 */
[[nodiscard]] constexpr parse_error with_note(parse_error error, microfmt::string_view note,
                                              const token &where) noexcept {
  error.note = note;
  error.note_at = where;
  return error;
}

/**
 * @brief Lookahead cursor over a lexed text.
 *
 * Borrows the source like `lexer`. Trivially copyable: `auto saved = ts; ...; ts = saved;`
 * is the backtracking mechanism. Once the input is exhausted every read
 * returns a `token_kind::end` token, so loops that always consume terminate.
 */
class RELOCO_POINTER token_stream {
public:
  /** @param source Text to tokenize; must outlive the stream. @param rules Language description (copied). */
  explicit constexpr token_stream(microfmt::string_view source RELOCO_LIFETIMEBOUND, const lexer_rules &rules) noexcept
      : source_(source), lexer_(source, rules), first_(lexer_.next()), second_(lexer_.next()) {}
  template <typename S, std::enable_if_t<std::is_same_v<S, std::string>, int> = 0>
  explicit token_stream(S &&, const lexer_rules &) = delete;

  /** Next token, not consumed. */
  [[nodiscard]] constexpr token peek() const noexcept { return first_; }

  /** The token after `peek()`, not consumed. */
  [[nodiscard]] constexpr token peek2() const noexcept { return second_; }

  /** Consumes and returns the next token (`end` forever once exhausted). */
  constexpr token next() noexcept {
    const token result = first_;
    if (result.kind != token_kind::end) {
      previous_ = result;
    }
    first_ = second_;
    second_ = lexer_.next();
    return result;
  }

  /** The last token consumed, or a default (`end`) token when nothing has been consumed yet. */
  [[nodiscard]] constexpr token previous() const noexcept { return previous_; }

  /** The whole text being parsed (what `source_span::text` is sliced from). */
  [[nodiscard]] constexpr microfmt::string_view source() const noexcept { return source_; }

  /** True when the next token is `end`. */
  [[nodiscard]] constexpr bool at_end() const noexcept { return first_.kind == token_kind::end; }

  [[nodiscard]] constexpr bool check(token_kind kind) const noexcept { return first_.kind == kind; }
  [[nodiscard]] constexpr bool check(token_kind kind, microfmt::string_view text) const noexcept {
    return first_.kind == kind && first_.text == text;
  }

  /** Consumes and returns the next token only when its kind matches; otherwise leaves it. */
  [[nodiscard]] RELOCO_CONSTEXPR20 reloco::optional<token> accept(token_kind kind) noexcept {
    if (!check(kind)) {
      return reloco::nullopt;
    }
    return next();
  }

  /** Like `accept(kind)`, but the spelling must also equal `text` (e.g. `punctuation` `"{"`). */
  [[nodiscard]] RELOCO_CONSTEXPR20 reloco::optional<token> accept(token_kind kind, microfmt::string_view text) noexcept {
    if (!check(kind, text)) {
      return reloco::nullopt;
    }
    return next();
  }

  /** Like `accept(kind)` but only reports whether a token was consumed; for optional pieces whose value is unused. */
  constexpr bool skip(token_kind kind) noexcept {
    if (!check(kind)) {
      return false;
    }
    next();
    return true;
  }

  /** Like `accept(kind, text)` but only reports whether a token was consumed. */
  constexpr bool skip(token_kind kind, microfmt::string_view text) noexcept {
    if (!check(kind, text)) {
      return false;
    }
    next();
    return true;
  }

  /** Consumes the next token if its kind matches, otherwise returns `parse_error{message, peek()}`. */
  [[nodiscard]] RELOCO_CONSTEXPR20 reloco::expected<token, parse_error>
  expect(token_kind kind, microfmt::string_view message) noexcept {
    if (!check(kind)) {
      return reloco::unexpected<parse_error>(error_here(message));
    }
    return next();
  }

  /** Like `expect(kind, message)`, but the spelling must also equal `text`. */
  [[nodiscard]] RELOCO_CONSTEXPR20 reloco::expected<token, parse_error>
  expect(token_kind kind, microfmt::string_view text, microfmt::string_view message) noexcept {
    if (!check(kind, text)) {
      return reloco::unexpected<parse_error>(error_here(message));
    }
    return next();
  }

  /** Builds an error located at the next token without consuming it. */
  [[nodiscard]] constexpr parse_error error_here(microfmt::string_view message) const noexcept {
    return parse_error{message, first_};
  }

private:
  microfmt::string_view source_;
  lexer lexer_;
  token first_;
  token second_;
  token previous_{};
};

/**
 * @brief RAII recursion guard for nested grammar rules.
 *
 * Increments a caller-owned counter on construction and decrements it on
 * destruction. `ok()` is false once nesting exceeds `limit`, so hostile input
 * such as `[[[[...` cannot overflow a small stack.
 */
class RELOCO_POINTER depth_guard {
public:
  constexpr depth_guard(size_t &depth RELOCO_LIFETIMEBOUND, size_t limit) noexcept
      : depth_(depth), ok_(++depth_ <= limit) {}
  RELOCO_CONSTEXPR20 ~depth_guard() { --depth_; }
  depth_guard(const depth_guard &) = delete;
  depth_guard &operator=(const depth_guard &) = delete;

  [[nodiscard]] constexpr bool ok() const noexcept { return ok_; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return ok_; }

private:
  size_t &depth_;
  bool ok_{false};
};

// ============================================================================
// Expressions (precedence climbing)
// ============================================================================

/** Operator associativity: `1 - 2 - 3` is `(1 - 2) - 3` for `left`, `1 ^ 2 ^ 3` is `1 ^ (2 ^ 3)` for `right`. */
enum class assoc : uint8_t { left, right };

/** One binary operator, e.g. `{"*", 20, assoc::left}`. Higher precedence binds tighter. */
struct RELOCO_POINTER binary_op {
  microfmt::string_view text{};
  uint8_t precedence{0};
  assoc associativity{assoc::left};
};

/** One prefix operator, e.g. `{"-", 30}`. Its operand is parsed at the operator's precedence. */
struct RELOCO_POINTER prefix_op {
  microfmt::string_view text{};
  uint8_t precedence{0};
};

/**
 * One postfix operator, e.g. `{"!", 50}`, `{"(", 60}` (call) or `{"[", 60}` (index).
 * Matched by spelling against `op`, `keyword` and `punctuation` tokens.
 */
struct RELOCO_POINTER postfix_op {
  microfmt::string_view text{};
  uint8_t precedence{0};
};

/**
 * Conditional operator `cond ? then : else`. Right associative: `a ? b : c ? d : e` is `a ? b : (c ? d : e)`.
 * Both spellings may be `op`, `keyword` or `punctuation` tokens.
 */
struct RELOCO_POINTER ternary_op {
  microfmt::string_view question{"?"};
  microfmt::string_view colon{":"};
  uint8_t precedence{5}; ///< Lower than the binary operators, so `a + b ? c : d` is `(a + b) ? c : d`.
};

/**
 * A keyword-introduced form such as `if c then a else b`, matched by the spelling of its first token
 * (`op`, `keyword`, `identifier` or `punctuation`) wherever an operand is expected.
 * `precedence` is a hint handed back to the callback for the trailing sub-expression.
 */
struct RELOCO_POINTER mixfix_op {
  microfmt::string_view text{};
  uint8_t precedence{0};
};

/** All operator tables for the `Actions` flavour of `parse_expression`; empty members mean "not used". */
struct RELOCO_POINTER expression_grammar {
  span<const binary_op> binary{};
  span<const prefix_op> prefix{};
  span<const postfix_op> postfix{};
  span<const mixfix_op> mixfix{};
  reloco::optional<ternary_op> ternary{};
};

namespace detail {

/** Ternary callback used when no ternary operator is given; never called because it is disabled. */
template <typename T> struct no_ternary {
  reloco::expected<T, parse_error> operator()(const ternary_op &, T, T, T) const noexcept {
    return reloco::unexpected<parse_error>(parse_error{"ternary operator is disabled", {}});
  }
};

/** Postfix callback used when no postfix operators are given; never called because the table is empty. */
template <typename T> struct no_postfix {
  reloco::expected<T, parse_error> operator()(const postfix_op &, T operand, token_stream &) const noexcept {
    return operand;
  }
};

/** Mixfix callback used when no mixfix forms are given; never called because the table is empty. */
template <typename T> struct no_mixfix {
  template <typename Expr>
  reloco::expected<T, parse_error> operator()(const mixfix_op &, token_stream &, Expr &) const noexcept {
    return reloco::unexpected<parse_error>(parse_error{"mixfix forms are disabled", {}});
  }
};

/** C++17 detection idiom: `is_detected<Op, Args...>` is true when `Op<Args...>` is a valid type. */
template <typename Void, template <typename...> class Op, typename... Args> struct detector : std::false_type {};
template <template <typename...> class Op, typename... Args>
struct detector<std::void_t<Op<Args...>>, Op, Args...> : std::true_type {};
template <template <typename...> class Op, typename... Args>
inline constexpr bool is_detected_v = detector<void, Op, Args...>::value;

template <typename A, typename... X> using binary_call_t = decltype(std::declval<A &>().make_binary(std::declval<X>()...));
template <typename A, typename... X> using prefix_call_t = decltype(std::declval<A &>().make_prefix(std::declval<X>()...));
template <typename A, typename... X> using postfix_call_t = decltype(std::declval<A &>().make_postfix(std::declval<X>()...));
template <typename A, typename... X> using ternary_call_t = decltype(std::declval<A &>().make_ternary(std::declval<X>()...));
template <typename A, typename... X> using mixfix_call_t = decltype(std::declval<A &>().make_mixfix(std::declval<X>()...));

/** Forwards to `actions.make_mixfix`, or fails when the actions object has none. */
template <typename T, typename Actions> struct RELOCO_POINTER mixfix_forwarder {
  Actions *actions;
  token_stream *stream;
  template <typename Expr>
  reloco::expected<T, parse_error> operator()(const mixfix_op &op, token_stream &s, Expr &expr) const noexcept {
    if constexpr (is_detected_v<mixfix_call_t, Actions, const mixfix_op &, token_stream &, Expr &>) {
      return actions->make_mixfix(op, s, expr);
    } else {
      return reloco::unexpected<parse_error>(stream->error_here("no make_mixfix callback"));
    }
  }
};

template <typename T, typename Primary, typename MakeBinary, typename MakePrefix, typename MakePostfix,
          typename MakeTernary, typename MakeMixfix = no_mixfix<T>>
class RELOCO_POINTER expression_parser {
public:
  expression_parser(token_stream &ts, span<const binary_op> binary, span<const prefix_op> prefix,
                    span<const postfix_op> postfix, const reloco::optional<ternary_op> &ternary RELOCO_LIFETIMEBOUND, Primary &primary,
                    MakeBinary &make_binary, MakePrefix &make_prefix, MakePostfix &make_postfix,
                    MakeTernary &make_ternary, size_t max_depth, span<const mixfix_op> mixfix = {},
                    MakeMixfix *make_mixfix = nullptr) noexcept
      : ts_(ts), binary_(binary), prefix_(prefix), postfix_(postfix), mixfix_(mixfix), ternary_(ternary),
        primary_(primary), make_binary_(make_binary), make_prefix_(make_prefix), make_postfix_(make_postfix),
        make_ternary_(make_ternary), make_mixfix_(make_mixfix), max_depth_(max_depth) {}

  [[nodiscard]] reloco::expected<T, parse_error> parse(unsigned min_precedence) noexcept {
    const depth_guard guard{depth_, max_depth_};
    if (!guard) {
      return reloco::unexpected<parse_error>(ts_.error_here("nesting too deep"));
    }
    auto left = parse_operand();
    if (!left) {
      return left;
    }
    T value = std::move(left).value();
    for (;;) {
      if (const reloco::optional<postfix_op> post = find_postfix(ts_.peek()); post && post->precedence >= min_precedence) {
        ts_.next();
        auto applied = make_postfix_(*post, std::move(value), ts_);
        if (!applied) {
          return applied;
        }
        value = std::move(applied).value();
        continue;
      }
      const reloco::optional<binary_op> found = find_binary(ts_.peek());
      if (!found) {
        if (ternary_ && ternary_->precedence >= min_precedence && has_spelling(ts_.peek(), ternary_->question)) {
          auto conditional = parse_ternary(std::move(value));
          if (!conditional) {
            return conditional;
          }
          value = std::move(conditional).value();
          continue;
        }
        return value;
      }
      if (found->precedence < min_precedence) {
        return value;
      }
      const binary_op op = *found;
      ts_.next();
      const unsigned next_min = op.associativity == assoc::right ? op.precedence : op.precedence + 1U;
      auto right = parse(next_min);
      if (!right) {
        return right;
      }
      auto combined = make_binary_(op, std::move(value), std::move(right).value());
      if (!combined) {
        return combined;
      }
      value = std::move(combined).value();
    }
  }

private:
  [[nodiscard]] static bool has_spelling(const token &tok, microfmt::string_view text) noexcept {
    return (tok.kind == token_kind::op || tok.kind == token_kind::keyword || tok.kind == token_kind::punctuation) &&
           tok.text == text;
  }

  [[nodiscard]] reloco::expected<T, parse_error> parse_ternary(T condition) noexcept {
    const ternary_op op = *ternary_;
    ts_.next(); // the question mark
    auto then_value = parse(0);
    if (!then_value) {
      return then_value;
    }
    if (!has_spelling(ts_.peek(), op.colon)) {
      return reloco::unexpected<parse_error>(ts_.error_here("expected ':'"));
    }
    ts_.next();
    auto else_value = parse(op.precedence); // same precedence: a nested ternary on the right is accepted
    if (!else_value) {
      return else_value;
    }
    return make_ternary_(op, std::move(condition), std::move(then_value).value(), std::move(else_value).value());
  }

  [[nodiscard]] static bool is_operator_token(const token &tok) noexcept {
    return tok.kind == token_kind::op || tok.kind == token_kind::keyword;
  }

  [[nodiscard]] reloco::optional<binary_op> find_binary(const token &tok) const noexcept {
    if (is_operator_token(tok)) {
      for (size_t i = 0; i < binary_.size(); ++i) {
        if (binary_[i].text == tok.text) {
          return binary_[i];
        }
      }
    }
    return reloco::nullopt;
  }

  [[nodiscard]] reloco::optional<prefix_op> find_prefix(const token &tok) const noexcept {
    if (is_operator_token(tok)) {
      for (size_t i = 0; i < prefix_.size(); ++i) {
        if (prefix_[i].text == tok.text) {
          return prefix_[i];
        }
      }
    }
    return reloco::nullopt;
  }

  [[nodiscard]] reloco::optional<postfix_op> find_postfix(const token &tok) const noexcept {
    if (is_operator_token(tok) || tok.kind == token_kind::punctuation) {
      for (size_t i = 0; i < postfix_.size(); ++i) {
        if (postfix_[i].text == tok.text) {
          return postfix_[i];
        }
      }
    }
    return reloco::nullopt;
  }

  [[nodiscard]] reloco::optional<mixfix_op> find_mixfix(const token &tok) const noexcept {
    if (is_operator_token(tok) || tok.kind == token_kind::identifier || tok.kind == token_kind::punctuation) {
      for (size_t i = 0; i < mixfix_.size(); ++i) {
        if (mixfix_[i].text == tok.text) {
          return mixfix_[i];
        }
      }
    }
    return reloco::nullopt;
  }

  [[nodiscard]] reloco::expected<T, parse_error> parse_operand() noexcept {
    if (const reloco::optional<mixfix_op> form = find_mixfix(ts_.peek()); form && make_mixfix_ != nullptr) {
      ts_.next();
      return (*make_mixfix_)(*form, ts_, *this);
    }
    if (const reloco::optional<prefix_op> op = find_prefix(ts_.peek())) {
      ts_.next();
      auto operand = parse(op->precedence);
      if (!operand) {
        return operand;
      }
      return make_prefix_(*op, std::move(operand).value());
    }
    if (ts_.skip(token_kind::punctuation, "(")) {
      auto inner = parse(0);
      if (!inner) {
        return inner;
      }
      if (auto close = ts_.expect(token_kind::punctuation, ")", "expected ')'"); !close) {
        return reloco::unexpected<parse_error>(close.error());
      }
      return inner;
    }
    return primary_(ts_);
  }

  token_stream &ts_;
  span<const binary_op> binary_;
  span<const prefix_op> prefix_;
  span<const postfix_op> postfix_;
  span<const mixfix_op> mixfix_;
  const reloco::optional<ternary_op> &ternary_;
  Primary &primary_;
  MakeBinary &make_binary_;
  MakePrefix &make_prefix_;
  MakePostfix &make_postfix_;
  MakeTernary &make_ternary_;
  MakeMixfix *make_mixfix_;
  size_t max_depth_;
  size_t depth_{0};
};

} // namespace detail

/**
 * @brief Parses an expression with operator precedence (precedence climbing); builds no AST.
 *
 * Why: without it every precedence level needs its own grammar function. You
 * describe the operators as tables and provide three callbacks that build
 * whatever value type `T` you want (a number, a node index, ...).
 *
 * @tparam T Result type of the expression (a number, an index into your node pool, ...).
 * @param ts Stream positioned at the start of the expression; left after its last token.
 * @param binary Binary operators (matched against `op` and `keyword` tokens by spelling).
 * @param prefix Prefix operators such as unary `-`.
 * @param primary `expected<T, parse_error>(token_stream &)`: parses a literal, identifier or call.
 *        `(` expr `)` is handled by this function and never reaches `primary`.
 * @param make_binary `expected<T, parse_error>(const binary_op &, T left, T right)`.
 * @param make_prefix `expected<T, parse_error>(const prefix_op &, T operand)`.
 * @param max_depth Nesting limit; deeper input fails with "nesting too deep".
 */
template <typename T, typename Primary, typename MakeBinary, typename MakePrefix>
[[nodiscard]] reloco::expected<T, parse_error>
parse_expression(token_stream &ts, span<const binary_op> binary, span<const prefix_op> prefix, Primary &&primary,
                 MakeBinary &&make_binary, MakePrefix &&make_prefix, size_t max_depth = 64) noexcept {
  detail::no_postfix<T> make_postfix{};
  detail::no_ternary<T> make_ternary{};
  const reloco::optional<ternary_op> no_ternary_op{};
  detail::expression_parser<T, Primary, MakeBinary, MakePrefix, detail::no_postfix<T>, detail::no_ternary<T>> parser{
      ts, binary, prefix, span<const postfix_op>{}, no_ternary_op, primary, make_binary, make_prefix, make_postfix,
      make_ternary, max_depth};
  return parser.parse(0);
}

/**
 * @brief `parse_expression` with postfix operators (calls, indexing, `x!`).
 *
 * Why: `f(x)` and `a[i]` cannot be written as prefix or binary operators. A
 * postfix operator binds to the operand on its left; `precedence` orders it
 * against the other operators (`-a!` is `-(a!)` when `!` binds tighter than
 * the prefix `-`, otherwise `(-a)!`).
 *
 * @param postfix Postfix operators; the spelling may be an `op`, `keyword` or `punctuation` token.
 * @param make_postfix `expected<T, parse_error>(const postfix_op &, T operand, token_stream &)`. The stream is
 *        positioned just after the operator spelling, so the callback parses call arguments or an index itself
 *        (typically with `parse_list` / `parse_expression`) and consumes the closing bracket.
 */
template <typename T, typename Primary, typename MakeBinary, typename MakePrefix, typename MakePostfix>
[[nodiscard]] reloco::expected<T, parse_error>
parse_expression(token_stream &ts, span<const binary_op> binary, span<const prefix_op> prefix,
                 span<const postfix_op> postfix, Primary &&primary, MakeBinary &&make_binary, MakePrefix &&make_prefix,
                 MakePostfix &&make_postfix, size_t max_depth = 64) noexcept {
  detail::no_ternary<T> make_ternary{};
  const reloco::optional<ternary_op> no_ternary_op{};
  detail::expression_parser<T, Primary, MakeBinary, MakePrefix, MakePostfix, detail::no_ternary<T>> parser{
      ts, binary, prefix, postfix, no_ternary_op, primary, make_binary, make_prefix, make_postfix, make_ternary,
      max_depth};
  return parser.parse(0);
}

/**
 * @brief `parse_expression` with postfix operators and the conditional operator `cond ? then : else`.
 *
 * Why: `?:` has three operands, so it cannot be a binary operator. The middle operand is a full
 * expression; the else part is parsed at the ternary precedence, which makes it right associative.
 *
 * @param ternary Spellings of `?` and `:` and the precedence (default 5, below the usual binary operators).
 * @param make_ternary `expected<T, parse_error>(const ternary_op &, T condition, T then_value, T else_value)`.
 *        Pass an empty `postfix` span when you only need `?:`.
 */
template <typename T, typename Primary, typename MakeBinary, typename MakePrefix, typename MakePostfix,
          typename MakeTernary>
[[nodiscard]] reloco::expected<T, parse_error>
parse_expression(token_stream &ts, span<const binary_op> binary, span<const prefix_op> prefix,
                 span<const postfix_op> postfix, const ternary_op &ternary, Primary &&primary,
                 MakeBinary &&make_binary, MakePrefix &&make_prefix, MakePostfix &&make_postfix,
                 MakeTernary &&make_ternary, size_t max_depth = 64) noexcept {
  const reloco::optional<ternary_op> enabled{ternary};
  detail::expression_parser<T, Primary, MakeBinary, MakePrefix, MakePostfix, MakeTernary> parser{
      ts, binary, prefix, postfix, enabled, primary, make_binary, make_prefix, make_postfix, make_ternary, max_depth};
  return parser.parse(0);
}

/**
 * @brief `parse_expression` driven by an `expression_grammar` and one `actions` object.
 *
 * Why: forms like `if c then a else b` or `let x = e in body` start with a keyword and contain several
 * sub-expressions, which `primary` cannot parse because it cannot re-enter the precedence parser. A
 * mixfix callback receives the stream and the running parser: `expr.parse(min_precedence)` parses a
 * sub-expression, and the callback consumes the keywords between them itself.
 *
 * `actions` members (all called with the same shapes as the other overloads):
 *  - `primary(ts)` is required;
 *  - `make_binary(op, l, r)` and `make_prefix(op, v)` are required when those tables are non-empty;
 *  - `make_postfix(op, v, ts)`, `make_ternary(op, c, t, e)` and `make_mixfix(op, ts, expr)` may be omitted;
 *    if the grammar uses the matching feature anyway, parsing fails with an error at that token.
 * A mixfix keyword takes priority over `primary`, so it cannot also be a variable name.
 */
template <typename T, typename Actions>
[[nodiscard]] reloco::expected<T, parse_error>
parse_expression(token_stream &ts, const expression_grammar &grammar, Actions &actions, size_t max_depth = 64) noexcept {
  using result = reloco::expected<T, parse_error>;
  auto primary = [&actions](token_stream &s) noexcept -> result { return actions.primary(s); };
  auto make_binary = [&actions, &ts](const binary_op &op, T l, T r) noexcept -> result {
    (void)ts;
    if constexpr (detail::is_detected_v<detail::binary_call_t, Actions, const binary_op &, T, T>) {
      return actions.make_binary(op, std::move(l), std::move(r));
    } else {
      (void)actions;
      return reloco::unexpected<parse_error>(ts.error_here("no make_binary callback"));
    }
  };
  auto make_prefix = [&actions, &ts](const prefix_op &op, T v) noexcept -> result {
    (void)ts;
    if constexpr (detail::is_detected_v<detail::prefix_call_t, Actions, const prefix_op &, T>) {
      return actions.make_prefix(op, std::move(v));
    } else {
      (void)actions;
      return reloco::unexpected<parse_error>(ts.error_here("no make_prefix callback"));
    }
  };
  auto make_postfix = [&actions, &ts](const postfix_op &op, T v, token_stream &s) noexcept -> result {
    (void)ts;
    if constexpr (detail::is_detected_v<detail::postfix_call_t, Actions, const postfix_op &, T, token_stream &>) {
      return actions.make_postfix(op, std::move(v), s);
    } else {
      (void)actions;
      return reloco::unexpected<parse_error>(ts.error_here("no make_postfix callback"));
    }
  };
  auto make_ternary = [&actions, &ts](const ternary_op &op, T c, T t, T e) noexcept -> result {
    (void)ts;
    if constexpr (detail::is_detected_v<detail::ternary_call_t, Actions, const ternary_op &, T, T, T>) {
      return actions.make_ternary(op, std::move(c), std::move(t), std::move(e));
    } else {
      (void)actions;
      return reloco::unexpected<parse_error>(ts.error_here("no make_ternary callback"));
    }
  };
  using adapted_parser = detail::expression_parser<T, decltype(primary), decltype(make_binary), decltype(make_prefix),
                                                   decltype(make_postfix), decltype(make_ternary),
                                                   detail::mixfix_forwarder<T, Actions>>;
  detail::mixfix_forwarder<T, Actions> adapter{&actions, &ts};
  adapted_parser parser{ts,       grammar.binary, grammar.prefix, grammar.postfix, grammar.ternary,
                        primary,  make_binary,    make_prefix,    make_postfix,    make_ternary,
                        max_depth, grammar.mixfix, &adapter};
  return parser.parse(0);
}

// ============================================================================
// Source spans
// ============================================================================

/**
 * The source range a rule consumed: exact text (no surrounding whitespace or comments) plus the
 * offset, line and column of its first token. `text` borrows from the parsed source.
 */
struct RELOCO_POINTER source_span {
  microfmt::string_view text{};
  size_t offset{0};
  uint32_t line{1};
  uint32_t column{1};
};

/**
 * @brief Span from `first` to the end of the last token consumed since.
 *
 * Why: a node built by a rule needs its source range for later diagnostics. Capture
 * `ts.peek()` before the rule, call this after it. Nothing consumed gives an empty span at `first`.
 */
[[nodiscard]] constexpr source_span span_from(const token_stream &ts, const token &first) noexcept {
  const token last = ts.previous();
  const size_t last_end = last.offset + last.text.size();
  if (last.kind == token_kind::end || last.offset < first.offset || last_end > ts.source().size() ||
      first.offset > ts.source().size()) {
    return source_span{{}, first.offset, first.line, first.column};
  }
  return source_span{ts.source().substr(first.offset, last_end - first.offset), first.offset, first.line,
                     first.column};
}

/** A rule's value together with the source range it consumed. */
template <typename T> struct RELOCO_POINTER spanned {
  T value;
  source_span span;
};

/**
 * @brief Runs `rule(ts)` and pairs its value with the span it consumed; errors pass through unchanged.
 *
 * @param rule `expected<T, parse_error>(token_stream &)`.
 */
template <typename Rule>
[[nodiscard]] RELOCO_CONSTEXPR20 auto parse_spanned(token_stream &ts, Rule &&rule) noexcept
    -> reloco::expected<spanned<typename std::decay_t<decltype(rule(ts))>::value_type>, parse_error> {
  using value_type = typename std::decay_t<decltype(rule(ts))>::value_type;
  const token first = ts.peek();
  auto result = rule(ts);
  if (!result) {
    return reloco::unexpected<parse_error>(result.error());
  }
  return spanned<value_type>{std::move(result).value(), span_from(ts, first)};
}

/**
 * @brief Parses `item (separator item)*` and folds left to right: `a.b.c` is `((a . b) . c)`.
 *
 * Why: chains such as paths (`a.b.c`), unions (`x | y`) or scopes (`f::g`) are a loop, not a
 * precedence problem. A separator that is not followed by an item is an error at that item
 * (the separator is consumed, so there is no silent fall-back to a shorter chain).
 *
 * @param item `expected<T, parse_error>(token_stream &)`.
 * @param fold `expected<T, parse_error>(const token &separator, T left, T right)`.
 * @param max_items Limit on the number of items; more fails with "too many items in chain".
 */
template <typename T, typename Item, typename Fold>
[[nodiscard]] RELOCO_CONSTEXPR20 reloco::expected<T, parse_error>
parse_chain(token_stream &ts, token_kind separator_kind, microfmt::string_view separator, Item &&item, Fold &&fold,
            size_t max_items = static_cast<size_t>(-1)) noexcept {
  auto first = item(ts);
  if (!first) {
    return first;
  }
  T value = std::move(first).value();
  size_t items = 1;
  for (;;) {
    const reloco::optional<token> sep = ts.accept(separator_kind, separator);
    if (!sep) {
      return value;
    }
    if (items >= max_items) {
      return reloco::unexpected<parse_error>(parse_error{"too many items in chain", *sep});
    }
    auto next_item = item(ts);
    if (!next_item) {
      return next_item;
    }
    auto folded = fold(*sep, std::move(value), std::move(next_item).value());
    if (!folded) {
      return folded;
    }
    value = std::move(folded).value();
    ++items;
  }
}

// ============================================================================
// Delimited lists
// ============================================================================

/** Shape of a delimited list. Brackets and separator are `punctuation` spellings. */
struct RELOCO_POINTER list_options {
  microfmt::string_view open{"["};      ///< Opening bracket; empty means a bare list with no opener.
  microfmt::string_view close{"]"};     ///< Closing bracket; empty means the list ends after the last item.
  microfmt::string_view separator{","}; ///< Separator between items.
  bool allow_trailing{false};           ///< Accept `[1, 2,]`.
};

/**
 * @brief Parses `open item {separator item} close`, calling `item(ts)` for each element.
 *
 * Why: arrays, call arguments and objects all repeat this loop. `item` is
 * `expected<void, parse_error>(token_stream &)` and must consume its element.
 * An empty list is accepted when `close` follows `open` directly. The loop
 * only continues after consuming a separator, so it always terminates.
 */
template <typename Item>
[[nodiscard]] reloco::expected<void, parse_error> parse_list(token_stream &ts, const list_options &opt,
                                                              Item &&item) noexcept {
  if (!opt.open.empty()) {
    if (auto open = ts.expect(token_kind::punctuation, opt.open, "expected opening bracket"); !open) {
      return reloco::unexpected<parse_error>(open.error());
    }
  }
  const bool bracketed = !opt.close.empty();
  if (bracketed && ts.skip(token_kind::punctuation, opt.close)) {
    return {};
  }
  for (;;) {
    if (auto element = item(ts); !element) {
      return element;
    }
    if (!ts.skip(token_kind::punctuation, opt.separator)) {
      break;
    }
    if (opt.allow_trailing && bracketed && ts.skip(token_kind::punctuation, opt.close)) {
      return {};
    }
  }
  if (bracketed) {
    if (auto close = ts.expect(token_kind::punctuation, opt.close, "expected separator or closing bracket"); !close) {
      return reloco::unexpected<parse_error>(close.error());
    }
  }
  return {};
}

// ============================================================================
// Error recovery
// ============================================================================

/** A synchronisation point for `recover_to`: a token kind and, optionally, an exact spelling. */
struct RELOCO_POINTER token_sync {
  token_kind kind{token_kind::end};
  microfmt::string_view text{}; ///< Empty matches any spelling of `kind`.
};

/**
 * @brief Skips tokens until the next one matches a sync point (left unconsumed) or the input ends.
 *
 * Why: after a `parse_error` a parser can resume at a known-good place (for
 * example the next `;`) and report further errors in the same run. Every call
 * consumes at least one token unless the next token is already a sync point or
 * `end`, so a loop of "parse, on error recover, consume the sync token"
 * terminates.
 *
 * @return Number of tokens skipped.
 */
[[nodiscard]] inline size_t recover_to(token_stream &ts, span<const token_sync> sync) noexcept {
  size_t skipped = 0;
  while (!ts.at_end()) {
    const token tok = ts.peek();
    for (size_t i = 0; i < sync.size(); ++i) {
      if (sync[i].kind == tok.kind && (sync[i].text.empty() || sync[i].text == tok.text)) {
        return skipped;
      }
    }
    ts.next();
    ++skipped;
  }
  return skipped;
}

/** A bracket pair that `recover_balanced` treats as one unit. */
struct RELOCO_POINTER bracket_pair {
  token_kind open_kind{token_kind::end};
  microfmt::string_view open{};
  token_kind close_kind{token_kind::end};
  microfmt::string_view close{};
};

namespace detail {

// Shared engine for recover_balanced and parse_statements_until: `is_sync(token)` is only asked at depth 0.
template <typename IsSync>
size_t recover_balanced_if(token_stream &ts, IsSync &&is_sync, span<const bracket_pair> brackets,
                           size_t max_depth) noexcept {
  size_t skipped = 0;
  size_t depth = 0;
  while (!ts.at_end()) {
    const token tok = ts.peek();
    if (depth == 0 && is_sync(tok)) {
      return skipped;
    }
    bool opens = false;
    bool closes = false;
    for (size_t i = 0; i < brackets.size(); ++i) {
      opens = opens || (brackets[i].open_kind == tok.kind && brackets[i].open == tok.text);
      closes = closes || (brackets[i].close_kind == tok.kind && brackets[i].close == tok.text);
    }
    if (opens) {
      if (depth >= max_depth) {
        return skipped;
      }
      ++depth;
    } else if (closes && depth > 0) {
      --depth;
    }
    ts.next();
    ++skipped;
  }
  return skipped;
}

} // namespace detail

/**
 * @brief Like `recover_to`, but sync tokens inside nested brackets are ignored.
 *
 * Why: a `;` inside `{ ... }` is not the end of the outer statement. Skipping the whole bracketed
 * region stops recovery from resuming in the middle of a block and reporting bogus follow-up errors.
 * Sync points are only honoured at depth 0; a closing bracket with no opener is skipped unless it is
 * itself a sync point. If nesting would exceed `max_depth` recovery stops at the current token,
 * which keeps the work linear in the token count for hostile input.
 *
 * @return Number of tokens skipped.
 */
[[nodiscard]] inline size_t recover_balanced(token_stream &ts, span<const token_sync> sync,
                                             span<const bracket_pair> brackets, size_t max_depth = 64) noexcept {
  return detail::recover_balanced_if(
      ts,
      [sync](const token &tok) {
        for (size_t i = 0; i < sync.size(); ++i) {
          if (sync[i].kind == tok.kind && (sync[i].text.empty() || sync[i].text == tok.text)) {
            return true;
          }
        }
        return false;
      },
      brackets, max_depth);
}

// ============================================================================
// Statement sequences with bounded error collection
// ============================================================================

/**
 * @brief Fixed-capacity list of parse errors (inline storage, no heap).
 *
 * Errors beyond `N` are counted but not stored, so a hostile input cannot grow
 * memory; check `overflowed()` to tell the user more errors exist. Stored
 * errors borrow the parsed source through their tokens, so the list must not
 * outlive the text.
 */
template <size_t N> class RELOCO_POINTER error_list {
  static_assert(N > 0, "error_list needs room for at least one error");

public:
  /** Stores `error` while there is room; always counts it. */
  constexpr void add(const parse_error &error) noexcept {
    if (stored_ < N) {
      items_[stored_] = error;
      ++stored_;
    }
    ++total_;
  }

  [[nodiscard]] constexpr span<const parse_error> errors() const & noexcept RELOCO_LIFETIMEBOUND {
    return span<const parse_error>(items_.data(), stored_);
  }
  span<const parse_error> errors() const && = delete;

  /** Number of stored errors (at most `N`). */
  [[nodiscard]] constexpr size_t size() const noexcept { return stored_; }
  /** Number of errors added, including those that did not fit. */
  [[nodiscard]] constexpr size_t total() const noexcept { return total_; }
  [[nodiscard]] constexpr bool overflowed() const noexcept { return total_ > stored_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return total_ == 0; }

private:
  reloco::array<parse_error, N> items_{};
  size_t stored_{0};
  size_t total_{0};
};

/**
 * @brief `parse_list` that records a failing item in `errors`, skips it and checks the rest.
 *
 * Why: one bad argument should not hide the next one. After an item error the tokens up to the next
 * separator or closing bracket at nesting depth 0 are skipped (bracket-aware, like `recover_balanced`)
 * and the loop goes on, so several mistakes in one list are reported in a single run.
 *
 * @param errors Receives the error of every failed item.
 * @param brackets Pairs skipped as units while recovering; empty when elements hold no nested brackets.
 * @return The number of failed items (0 for a clean list). Problems that cannot be recovered locally
 *         (missing opening bracket, input ending before the closer) are returned as an error and NOT
 *         added to `errors`, so the caller reports them once. A caller must treat a non-zero count as
 *         "the list is not usable".
 */
template <size_t N, typename Item>
[[nodiscard]] reloco::expected<size_t, parse_error>
parse_list_recovering(token_stream &ts, const list_options &opt, error_list<N> &errors,
                      span<const bracket_pair> brackets, Item &&item, size_t max_depth = 64) noexcept {
  if (!opt.open.empty()) {
    if (auto open = ts.expect(token_kind::punctuation, opt.open, "expected opening bracket"); !open) {
      return reloco::unexpected<parse_error>(open.error());
    }
  }
  const bool bracketed = !opt.close.empty();
  if (bracketed && ts.skip(token_kind::punctuation, opt.close)) {
    return size_t{0};
  }
  auto is_sync = [&opt, bracketed](const token &tok) noexcept {
    return tok.kind == token_kind::punctuation && (tok.text == opt.separator || (bracketed && tok.text == opt.close));
  };
  size_t failed = 0;
  for (;;) {
    if (auto element = item(ts); !element) {
      errors.add(element.error());
      ++failed;
      (void)detail::recover_balanced_if(ts, is_sync, brackets, max_depth);
      // Recovery can stop early (nesting cap); skipping one token guarantees progress.
      if (!ts.at_end() && !is_sync(ts.peek())) {
        ts.next();
      }
    }
    if (!ts.skip(token_kind::punctuation, opt.separator)) {
      break;
    }
    if (opt.allow_trailing && bracketed && ts.skip(token_kind::punctuation, opt.close)) {
      return failed;
    }
  }
  if (bracketed) {
    if (auto close = ts.expect(token_kind::punctuation, opt.close, "expected separator or closing bracket"); !close) {
      return reloco::unexpected<parse_error>(close.error());
    }
  }
  return failed;
}

namespace detail {

template <size_t N, typename Statement, typename Recover>
size_t statements_loop(token_stream &ts, error_list<N> &errors, Statement &statement, Recover &&recover) noexcept {
  size_t parsed = 0;
  while (!ts.at_end()) {
    const size_t start = ts.peek().offset;
    auto result = statement(ts);
    if (result) {
      ++parsed;
      if (!ts.at_end() && ts.peek().offset == start) {
        ts.next();
      }
      continue;
    }
    errors.add(result.error());
    recover(ts);
    if (ts.at_end()) {
      break;
    }
    ts.next();
  }
  return parsed;
}

} // namespace detail

/**
 * @brief Runs `statement(ts)` until the input ends, collecting every error instead of stopping at the first.
 *
 * Why: this packages the usual "parse, on error store it, skip to a safe token,
 * continue" loop. On an error the error is added to `errors`, tokens are skipped up
 * to the next `sync` token (see `recover_to`) and that token is consumed. If no
 * sync token remains, parsing ends. A statement that succeeds without consuming
 * anything is skipped by one token, so the loop always terminates.
 *
 * @param statement `expected<void, parse_error>(token_stream &)`; parses one statement.
 * @return Number of statements parsed successfully.
 */
template <size_t N, typename Statement>
[[nodiscard]] size_t parse_statements(token_stream &ts, span<const token_sync> sync, error_list<N> &errors,
                                      Statement &&statement) noexcept {
  return detail::statements_loop(ts, errors, statement, [sync](token_stream &s) { (void)recover_to(s, sync); });
}

/**
 * @brief `parse_statements` that recovers with `recover_balanced`, so a sync token inside a nested
 * bracket pair never ends recovery early. Use it when statements can contain `{ ... }` blocks.
 */
template <size_t N, typename Statement>
[[nodiscard]] size_t parse_statements(token_stream &ts, span<const token_sync> sync, span<const bracket_pair> brackets,
                                      error_list<N> &errors, Statement &&statement) noexcept {
  return detail::statements_loop(ts, errors, statement,
                                 [sync, brackets](token_stream &s) { (void)recover_balanced(s, sync, brackets); });
}

/**
 * @brief Parses statements until `close` (left unconsumed), collecting errors into `errors`.
 *
 * Why: a block rule that has consumed `{` must keep recovering inside its own body, otherwise the
 * outer loop resumes mid-block and reports bogus follow-up errors. After a statement error this skips
 * to the next sync token (or `close`) at bracket depth 0, so the block's closing bracket is never
 * swallowed. Reaching the end of input before `close` records "unexpected end of input". A body error
 * does not fail the caller: it is recorded and the block counts as parsed, so compare `errors.total()`
 * before and after if you need to know. Progress is guaranteed as in `parse_statements`.
 *
 * @return Number of statements parsed successfully.
 */
template <size_t N, typename Statement>
[[nodiscard]] size_t parse_statements_until(token_stream &ts, token_kind close_kind, microfmt::string_view close,
                                            span<const token_sync> sync, span<const bracket_pair> brackets,
                                            error_list<N> &errors, Statement &&statement) noexcept {
  size_t parsed = 0;
  for (;;) {
    if (ts.at_end()) {
      errors.add(parse_error{"unexpected end of input", ts.peek()});
      break;
    }
    if (ts.check(close_kind, close)) {
      break;
    }
    const size_t start = ts.peek().offset;
    auto result = statement(ts);
    if (result) {
      ++parsed;
      if (ts.peek().offset == start && !ts.at_end() && !ts.check(close_kind, close)) {
        ts.next();
      }
      continue;
    }
    errors.add(result.error());
    (void)detail::recover_balanced_if(
        ts,
        [close_kind, close, sync](const token &tok) {
          if (tok.kind == close_kind && tok.text == close) {
            return true;
          }
          for (size_t i = 0; i < sync.size(); ++i) {
            if (sync[i].kind == tok.kind && (sync[i].text.empty() || sync[i].text == tok.text)) {
              return true;
            }
          }
          return false;
        },
        brackets, 64);
    if (!ts.at_end() && !ts.check(close_kind, close)) {
      ts.next();
    }
  }
  return parsed;
}

/**
 * @brief Consumes one token of any of `kinds`; the error uses the caller's static `message`.
 *
 * Why: a rule that accepts several kinds ("integer, string or identifier") can say exactly that. The
 * message is a literal rather than generated text because errors borrow static storage and nothing allocates.
 */
[[nodiscard]] RELOCO_CONSTEXPR20 inline reloco::expected<token, parse_error>
expect_one_of(token_stream &ts, span<const token_kind> kinds, microfmt::string_view message) noexcept {
  for (size_t i = 0; i < kinds.size(); ++i) {
    if (ts.check(kinds[i])) {
      return ts.next();
    }
  }
  return reloco::unexpected<parse_error>(ts.error_here(message));
}

/**
 * @brief Runs `rule(ts)` and restores the stream if it fails.
 *
 * Why: "try this, else undo" is the building block of backtracking; doing it here means no
 * failure path can leave the stream half-consumed. On success the consumed tokens stay consumed.
 *
 * @param rule `expected<T, parse_error>(token_stream &)`.
 */
template <typename Rule>
[[nodiscard]] RELOCO_CONSTEXPR20 auto attempt(token_stream &ts, Rule &&rule) noexcept -> std::decay_t<decltype(rule(ts))> {
  const token_stream saved = ts;
  auto result = rule(ts);
  if (!result) {
    ts = saved;
  }
  return result;
}

namespace detail {

template <typename R, typename Rule, typename... Rest>
RELOCO_CONSTEXPR20 R choice_step(token_stream &ts, const token_stream &saved, reloco::optional<parse_error> best,
                                 Rule &&rule, Rest &&...rest) noexcept {
  ts = saved;
  R result = rule(ts);
  if (result) {
    return result;
  }
  // The alternative that got furthest is the likeliest intended one; ties keep the earlier.
  if (!best || result.error().at.offset > best->at.offset) {
    best = result.error();
  }
  if constexpr (sizeof...(Rest) == 0) {
    ts = saved;
    return R(reloco::unexpected<parse_error>(*best));
  } else {
    return choice_step<R>(ts, saved, std::move(best), rest...);
  }
}

} // namespace detail

/**
 * @brief Ordered choice: tries each rule from the same position and returns the first success.
 *
 * Why: grammars with alternatives ("a declaration is a function, a variable or an alias")
 * need rewinding between attempts. If every rule fails, the stream is restored and the
 * error of the alternative that got furthest (largest token offset) is returned.
 *
 * @param rules at least one `expected<T, parse_error>(token_stream &)`; all must return the same `T`.
 */
template <typename First, typename... Rest>
[[nodiscard]] RELOCO_CONSTEXPR20 auto parse_choice(token_stream &ts, First &&first, Rest &&...rest) noexcept
    -> std::decay_t<decltype(first(ts))> {
  using result_type = std::decay_t<decltype(first(ts))>;
  const token_stream saved = ts;
  return detail::choice_step<result_type>(ts, saved, reloco::optional<parse_error>{}, first, rest...);
}

/**
 * @brief Zero-or-one: the rule's value, or an empty optional when it fails without consuming input.
 *
 * Why: optional grammar parts ("an optional `else` branch") must not hide real mistakes, so a
 * failure after the rule consumed tokens (`x = ` with no value) stays an error.
 *
 * @param rule `expected<T, parse_error>(token_stream &)`, `T` not void.
 */
template <typename Rule>
[[nodiscard]] RELOCO_CONSTEXPR20 auto parse_optional(token_stream &ts, Rule &&rule) noexcept
    -> reloco::expected<reloco::optional<typename std::decay_t<decltype(rule(ts))>::value_type>, parse_error> {
  using optional_type = reloco::optional<typename std::decay_t<decltype(rule(ts))>::value_type>;
  const token_stream saved = ts;
  const size_t start = ts.peek().offset;
  auto result = rule(ts);
  if (result) {
    return optional_type(std::move(result).value());
  }
  if (ts.peek().offset != start) {
    return reloco::unexpected<parse_error>(result.error());
  }
  ts = saved;
  return optional_type{};
}

/** Limits and error text for `parse_repeat`. */
struct RELOCO_POINTER repeat_options {
  size_t min{0};                                          ///< Fewest items required.
  size_t max{static_cast<size_t>(-1)};                    ///< Most items accepted; reaching it stops without error.
  microfmt::string_view too_few_message{"too few items"}; ///< Error text when fewer than `min` were parsed.
};

/**
 * @brief Repeats `item(ts)` while it succeeds and consumes input, handing each value to `sink`.
 *
 * Why: separator-less repetition ("zero or more statements") needs a progress check or an item
 * that consumes nothing loops forever. The caller owns the storage through `sink`, so no heap is used.
 * A failure that consumed nothing ends the loop cleanly (stream restored); a failure after consuming
 * input is a real mistake and is returned. An item that succeeds without consuming ends the loop and
 * its value is discarded.
 *
 * @param item `expected<T, parse_error>(token_stream &)`, `T` not void.
 * @param sink `void(T &&)`; called once per parsed item, in order.
 * @return The number of items parsed.
 */
template <typename Item, typename Sink>
[[nodiscard]] RELOCO_CONSTEXPR20 reloco::expected<size_t, parse_error>
parse_repeat(token_stream &ts, const repeat_options &opt, Item &&item, Sink &&sink) noexcept {
  size_t count = 0;
  while (count < opt.max) {
    const token_stream saved = ts;
    const size_t start = ts.peek().offset;
    auto result = item(ts);
    if (!result) {
      if (ts.peek().offset != start) {
        return reloco::unexpected<parse_error>(result.error());
      }
      ts = saved;
      break;
    }
    if (ts.peek().offset == start) {
      break;
    }
    sink(std::move(result).value());
    ++count;
  }
  if (count < opt.min) {
    return reloco::unexpected<parse_error>(parse_error{opt.too_few_message, ts.peek()});
  }
  return count;
}

/**
 * @brief Parses items until the terminator token (left unconsumed), handing each value to `sink`.
 *
 * Why: "statements until `}`" is the common block shape, and here any item failure is a real error
 * (the terminator has not been seen), unlike `parse_repeat`. Reaching the end of input without the
 * terminator is an error, and so is an item that consumes nothing (it would never reach the terminator).
 *
 * @param item `expected<T, parse_error>(token_stream &)`, `T` not void.
 * @param sink `void(T &&)`.
 * @return The number of items parsed.
 */
template <typename Item, typename Sink>
[[nodiscard]] RELOCO_CONSTEXPR20 reloco::expected<size_t, parse_error>
parse_until(token_stream &ts, token_kind kind, microfmt::string_view text, Item &&item, Sink &&sink) noexcept {
  size_t count = 0;
  while (!ts.check(kind, text)) {
    if (ts.at_end()) {
      return reloco::unexpected<parse_error>(ts.error_here("unexpected end of input"));
    }
    const size_t start = ts.peek().offset;
    auto result = item(ts);
    if (!result) {
      return reloco::unexpected<parse_error>(result.error());
    }
    if (ts.peek().offset == start) {
      return reloco::unexpected<parse_error>(ts.error_here("item consumed no input"));
    }
    sink(std::move(result).value());
    ++count;
  }
  return count;
}

} // namespace microfmt
