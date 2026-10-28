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

namespace detail {

template <typename T, typename Primary, typename MakeBinary, typename MakePrefix> class RELOCO_POINTER expression_parser {
public:
  expression_parser(token_stream &ts, span<const binary_op> binary, span<const prefix_op> prefix, Primary &primary,
                    MakeBinary &make_binary, MakePrefix &make_prefix, size_t max_depth) noexcept
      : ts_(ts), binary_(binary), prefix_(prefix), primary_(primary), make_binary_(make_binary),
        make_prefix_(make_prefix), max_depth_(max_depth) {}

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
      const reloco::optional<binary_op> found = find_binary(ts_.peek());
      if (!found || found->precedence < min_precedence) {
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
  Primary &primary_;
  MakeBinary &make_binary_;
  MakePrefix &make_prefix_;
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
  detail::expression_parser<T, Primary, MakeBinary, MakePrefix> parser{ts, binary, prefix, primary, make_binary,
                                                                        make_prefix, max_depth};
  return parser.parse(0);
}

} // namespace microfmt
