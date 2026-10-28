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
};

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
      : lexer_(source, rules), first_(lexer_.next()), second_(lexer_.next()) {}
  template <typename S, std::enable_if_t<std::is_same_v<S, std::string>, int> = 0>
  explicit token_stream(S &&, const lexer_rules &) = delete;

  /** Next token, not consumed. */
  [[nodiscard]] constexpr token peek() const noexcept { return first_; }

  /** The token after `peek()`, not consumed. */
  [[nodiscard]] constexpr token peek2() const noexcept { return second_; }

  /** Consumes and returns the next token (`end` forever once exhausted). */
  constexpr token next() noexcept {
    const token result = first_;
    first_ = second_;
    second_ = lexer_.next();
    return result;
  }

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
  lexer lexer_;
  token first_;
  token second_;
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

template <typename T, typename Primary, typename MakeBinary, typename MakePrefix, typename MakePostfix,
          typename MakeTernary>
class RELOCO_POINTER expression_parser {
public:
  expression_parser(token_stream &ts, span<const binary_op> binary, span<const prefix_op> prefix,
                    span<const postfix_op> postfix, const reloco::optional<ternary_op> &ternary, Primary &primary,
                    MakeBinary &make_binary, MakePrefix &make_prefix, MakePostfix &make_postfix,
                    MakeTernary &make_ternary, size_t max_depth) noexcept
      : ts_(ts), binary_(binary), prefix_(prefix), postfix_(postfix), ternary_(ternary), primary_(primary),
        make_binary_(make_binary), make_prefix_(make_prefix), make_postfix_(make_postfix),
        make_ternary_(make_ternary), max_depth_(max_depth) {}

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

  [[nodiscard]] reloco::expected<T, parse_error> parse_operand() noexcept {
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
  const reloco::optional<ternary_op> &ternary_;
  Primary &primary_;
  MakeBinary &make_binary_;
  MakePrefix &make_prefix_;
  MakePostfix &make_postfix_;
  MakeTernary &make_ternary_;
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
    (void)recover_to(ts, sync);
    if (ts.at_end()) {
      break;
    }
    ts.next();
  }
  return parsed;
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
