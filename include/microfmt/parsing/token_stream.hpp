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
 * for the user guide and `docs/design/token-stream.md` for the design.
 */

#include "lexer.hpp"
#include <reloco/expected.hpp>
#include <reloco/optional.hpp>
#include <string>
#include <type_traits>

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

} // namespace microfmt
