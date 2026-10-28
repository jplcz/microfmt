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
#include "parse_error.hpp"
#include "stream_window.hpp"
#include "string_pool_ref.hpp"
#include <reloco/array.hpp>
#include <reloco/expected.hpp>
#include <reloco/optional.hpp>
#include <string>
#include <type_traits>
#include <utility>

namespace microfmt {

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

  /** Like the constructor above, and `strings` is where `intern`, `expect_name` and `expect_string` store text. */
  explicit constexpr token_stream(microfmt::string_view source RELOCO_LIFETIMEBOUND, const lexer_rules &rules,
                                  const string_pool_ref &strings RELOCO_LIFETIMEBOUND) noexcept
      : token_stream(source, rules) {
    strings_ = reloco::value_ptr<const string_pool_ref>(&strings);
  }

  /**
   * Streaming input: tokenizes the text that arrives through `window` (see `stream_window`). Same parser helpers,
   * but token offsets are absolute (bytes since the start of the input) and `discard_consumed()` makes room.
   * The window must outlive the stream and every copy of it.
   */
  explicit token_stream(window_ref window RELOCO_LIFETIMEBOUND, const lexer_rules &rules) noexcept
      : source_(), lexer_(microfmt::string_view(), rules), window_(&window.state()),
        generation_(window.state().generation), margin_(stream_margin(rules)) {
    refill();
  }

  /** Streaming input plus a string pool for text that must outlive the window. */
  explicit token_stream(window_ref window RELOCO_LIFETIMEBOUND, const lexer_rules &rules,
                        const string_pool_ref &strings RELOCO_LIFETIMEBOUND) noexcept
      : token_stream(window, rules) {
    strings_ = reloco::value_ptr<const string_pool_ref>(&strings);
  }

  /** Next token, not consumed. */
  [[nodiscard]] constexpr token peek() const noexcept { return first_; }

  /** The token after `peek()`, not consumed. */
  [[nodiscard]] constexpr token peek2() const noexcept { return second_; }

  /** Consumes and returns the next token (`end` forever once exhausted). */
  constexpr token next() noexcept {
    const token result = first_;
    if (result.kind != token_kind::end) {
      previous_ = result;
      after_previous_ = position_after_token(result);
    }
    first_ = second_;
    second_ = lex_next();
    return result;
  }

  /** The last token consumed, or a default (`end`) token when nothing has been consumed yet. */
  [[nodiscard]] constexpr token previous() const noexcept { return previous_; }

  /** The text currently held: the whole input, or for a streaming stream the window contents. */
  [[nodiscard]] constexpr microfmt::string_view source() const noexcept {
    return window_ ? window_->text() : source_;
  }

  /** Absolute offset of `source()[0]`: 0 unless the stream is streaming and has discarded input. */
  [[nodiscard]] constexpr size_t source_base() const noexcept { return window_ ? window_->base : 0; }

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

  /** The rules currently used to tokenize. */
  [[nodiscard]] constexpr const lexer_rules &rules() const noexcept RELOCO_LIFETIMEBOUND { return lexer_.rules(); }

  /** Switches the rules and re-tokenizes the lookahead (`peek()`, `peek2()`) from the end of the last consumed token. */
  constexpr void set_rules(const lexer_rules &rules) noexcept {
    lexer_.set_rules(rules);
    margin_ = stream_margin(rules);
    refill();
  }

  /**
   * Consumes the raw bytes after the last consumed token (whitespace included) up to, not including, the first byte
   * of `stop_bytes` or the end of the text, and returns them as a `string` token without quotes. The run may be
   * empty. The lookahead is re-tokenized after the raw text.
   *
   * Streaming: when the window fills before a stop byte appears, the piece reaches the window end and
   * `raw_cut()` is true; the lookahead is `end` until you call `take_raw_until` again, which first drops the
   * bytes already handed out (earlier pieces dangle) and continues with the next part of the run.
   */
  constexpr token take_raw_until(microfmt::string_view stop_bytes) noexcept {
    if (window_) {
      return take_raw_windowed(stop_bytes);
    }
    lexer_.restore(after_previous_);
    previous_ = lexer_.take_raw(stop_bytes);
    after_previous_ = lexer_.state();
    first_ = lexer_.next();
    second_ = lexer_.next();
    return previous_;
  }

  /** True when the last `take_raw_until` stopped at the end of a full window, not at a stop byte (streaming only). */
  [[nodiscard]] constexpr bool raw_cut() const noexcept { return cut_; }

  /** True when a token did not fit in the window since the last discard: the window is too small for the input. */
  [[nodiscard]] constexpr bool window_too_small() const noexcept { return window_ && window_->overflowed; }

  /** True when a string pool was supplied to the constructor. */
  [[nodiscard]] constexpr bool has_strings() const noexcept { return static_cast<bool>(strings_); }

  /** The string pool given to the constructor; check `has_strings()` first. */
  [[nodiscard]] constexpr const string_pool_ref &strings() const noexcept RELOCO_LIFETIMEBOUND { return *strings_; }

  /**
   * Streaming: drops the input before the line of the last consumed token and re-tokenizes the lookahead, making
   * room in the window. Do it only between statements, never inside `attempt`/`parse_choice`:
   *
   * - every `token`, `string_view` and `parse_error` that pointed into the window dangles, and `previous().text`
   *   is cleared (its position stays); keep text through the string pool (`intern`, `store`) before discarding;
   * - copies of the stream made earlier become stale and yield `end` tokens (they must not be used again);
   * - a `memo_table` must be cleared (`end_statement` does this).
   *
   * No effect without a window. Offsets, lines and columns stay absolute.
   */
  void discard_consumed() noexcept {
    if (!window_ || window_->generation != generation_) {
      return;
    }
    const microfmt::string_view text = window_->text();
    const size_t consumed = after_previous_.pos < text.size() ? after_previous_.pos : text.size();
    size_t keep = consumed;
    while (keep > 0 && text[keep - 1] != '\n') {
      --keep;
    }
    // A very long line is not worth keeping for the error snippet: it would leave no room.
    if (consumed - keep > window_->buffer.size() / 2) {
      keep = consumed;
    }
    drop_bytes(keep);
    refill();
  }

private:
  // Smallest number of bytes that must follow a token before it is trusted: the lexer looks a little past a
  // token to decide its kind (`1.` then a digit, `1e+5`, the longest operator, a date-time, a `[==[` level).
  // Floor 16, cap 64 (the minimum window is 4 times that).
  [[nodiscard]] static constexpr size_t stream_margin(const lexer_rules &rules) noexcept {
    size_t margin = 16;
    const auto grow = [&margin](size_t wanted) { margin = wanted > margin ? wanted : margin; };
    for (size_t i = 0; i < rules.operators.size(); ++i) {
      grow(rules.operators[i].size() + 2);
    }
    for (size_t i = 0; i < rules.string_prefixes.size(); ++i) {
      grow(rules.string_prefixes[i].size() + 2);
    }
    for (size_t i = 0; i < rules.number_suffixes.size(); ++i) {
      grow(rules.number_suffixes[i].size() + 2);
    }
    grow(rules.block_comment_open.size() + 2);
    grow(rules.block_comment_close.size() + 2);
    if (rules.datetime_literals) {
      grow(40);
    }
    if (rules.cpp_raw_strings) {
      grow(24);
    }
    if (rules.long_bracket_strings || rules.long_bracket_comments) {
      grow(64);
    }
    return margin < 64 ? margin : 64;
  }

  [[nodiscard]] constexpr lexer::position position_after_token(const token &t) const noexcept {
    token relative = t;
    relative.offset -= source_base();
    return lexer_.position_after(relative);
  }

  // Next token with an absolute offset. Streaming: only returns a token once enough bytes follow it (or the input
  // ended), reading more and re-lexing until then.
  constexpr token lex_next() noexcept {
    if (!window_) {
      return lexer_.next();
    }
    return lex_windowed();
  }

  token lex_windowed() noexcept {
    window_state &w = *window_;
    if (w.generation != generation_) {
      return token{};
    }
    for (;;) {
      lexer_.set_source(w.text());
      const lexer::position start = lexer_.state();
      token t = lexer_.next();
      const size_t end = t.offset + t.text.size();
      if (w.eof || end + margin_ <= w.used) {
        t.offset += w.base;
        return t;
      }
      if (w.used >= w.buffer.size()) {
        return overflow_token(w, t);
      }
      const size_t want = end + margin_;
      w.fill(want < w.buffer.size() ? want : w.buffer.size());
      lexer_.restore(start);
    }
  }

  // The window is full and the token touches its end: hand back what is there as an `error` token (or `end` when
  // nothing is left) so the parser reports it, and remember that the window is too small.
  token overflow_token(window_state &w, token t) noexcept {
    w.overflowed = true;
    if (t.offset >= w.used) {
      t.kind = token_kind::end;
      t.text = microfmt::string_view();
    } else {
      t.kind = token_kind::error;
      t.text = w.text().substr(t.offset);
      lexer_.restore(lexer_.position_after(t));
    }
    t.offset += w.base;
    return t;
  }

  token take_raw_windowed(microfmt::string_view stop_bytes) noexcept {
    window_state &w = *window_;
    if (w.generation != generation_) {
      return token{};
    }
    const bool continuing = cut_;
    cut_ = false;
    if (continuing && w.used >= w.buffer.size()) {
      drop_bytes(after_previous_.pos < w.used ? after_previous_.pos : w.used);
    }
    lexer_.restore(after_previous_);
    token piece;
    for (;;) {
      lexer_.set_source(w.text());
      const lexer::position start = lexer_.state();
      piece = lexer_.take_raw(stop_bytes);
      const bool reached_end = piece.offset + piece.text.size() >= w.used;
      if (!reached_end || w.eof) {
        break;
      }
      if (w.used >= w.buffer.size()) {
        cut_ = true;
        break;
      }
      w.fill(w.buffer.size());
      lexer_.restore(start);
    }
    after_previous_ = lexer_.state();
    piece.offset += w.base;
    previous_ = piece;
    if (cut_) {
      first_ = token{};
      second_ = token{};
    } else {
      first_ = lex_next();
      second_ = lex_next();
    }
    return previous_;
  }

  // Removes the first `count` window bytes and keeps this stream consistent with the shifted window.
  void drop_bytes(size_t count) noexcept {
    if (count == 0) {
      return;
    }
    window_->discard(count);
    generation_ = window_->generation;
    after_previous_.pos -= count;
    previous_.text = microfmt::string_view();
  }

  constexpr void refill() noexcept {
    lexer_.restore(after_previous_);
    first_ = lex_next();
    second_ = lex_next();
  }

  microfmt::string_view source_;
  lexer lexer_;
  token first_;
  token second_;
  token previous_{};
  lexer::position after_previous_{};
  reloco::value_ptr<window_state> window_{};
  reloco::value_ptr<const string_pool_ref> strings_{};
  uint32_t generation_{0};
  size_t margin_{16};
  bool cut_{false};
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

/**
 * Operator associativity: `1 - 2 - 3` is `(1 - 2) - 3` for `left`, `1 ^ 2 ^ 3` is `1 ^ (2 ^ 3)` for `right`.
 * `none` forbids chaining: `a < b < c` is the parse error "operator is not associative".
 */
enum class assoc : uint8_t { left, right, none };

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
template <typename A, typename... X> using paren_call_t = decltype(std::declval<A &>().make_paren(std::declval<X>()...));

/** Forwards to `actions.make_paren`, or returns the inner value unchanged when the actions object has none. */
template <typename T, typename Actions> struct RELOCO_POINTER paren_forwarder {
  Actions *actions;
  reloco::expected<T, parse_error> operator()(const token &open, T inner, token_stream &s) const noexcept {
    if constexpr (is_detected_v<paren_call_t, Actions, const token &, T, token_stream &>) {
      return actions->make_paren(open, std::move(inner), s);
    } else {
      (void)actions;
      (void)open;
      (void)s;
      return inner;
    }
  }
};

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
          typename MakeTernary, typename MakeMixfix = no_mixfix<T>, typename MakeParen = void>
class RELOCO_POINTER expression_parser {
public:
  expression_parser(token_stream &ts, span<const binary_op> binary, span<const prefix_op> prefix,
                    span<const postfix_op> postfix, const reloco::optional<ternary_op> &ternary RELOCO_LIFETIMEBOUND, Primary &primary,
                    MakeBinary &make_binary, MakePrefix &make_prefix, MakePostfix &make_postfix,
                    MakeTernary &make_ternary, size_t max_depth, span<const mixfix_op> mixfix = {},
                    MakeMixfix *make_mixfix = nullptr, MakeParen *make_paren = nullptr) noexcept
      : ts_(ts), binary_(binary), prefix_(prefix), postfix_(postfix), mixfix_(mixfix), ternary_(ternary),
        primary_(primary), make_binary_(make_binary), make_prefix_(make_prefix), make_postfix_(make_postfix),
        make_ternary_(make_ternary), make_mixfix_(make_mixfix), make_paren_(make_paren), max_depth_(max_depth) {}

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
    int non_associative = -1; // precedence of the `none` operator just applied, or -1
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
      if (non_associative == static_cast<int>(op.precedence)) {
        return reloco::unexpected<parse_error>(ts_.error_here("operator is not associative"));
      }
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
      non_associative = op.associativity == assoc::none ? static_cast<int>(op.precedence) : -1;
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
    if (ts_.check(token_kind::punctuation, "(")) {
      const token open = ts_.next();
      auto inner = parse(0);
      if (!inner) {
        return inner;
      }
      if (auto close = ts_.expect(token_kind::punctuation, ")", "expected ')'"); !close) {
        return reloco::unexpected<parse_error>(close.error());
      }
      if constexpr (!std::is_void_v<MakeParen>) {
        if (make_paren_ != nullptr) {
          return (*make_paren_)(open, std::move(inner).value(), ts_);
        }
      }
      (void)open;
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
  [[maybe_unused]] MakeParen *make_paren_;
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
 *    if the grammar uses the matching feature anyway, parsing fails with an error at that token;
 *  - `make_paren(open_token, v, ts)` is optional too: it is called after `( expr )` with the inner value and the
 *    stream just past the `)`, so a language can record that the operand was parenthesised or continue with
 *    suffixes (`(f or g)(x)`). Without it the inner value is used as is.
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
                                                   detail::mixfix_forwarder<T, Actions>,
                                                   detail::paren_forwarder<T, Actions>>;
  detail::mixfix_forwarder<T, Actions> adapter{&actions, &ts};
  detail::paren_forwarder<T, Actions> paren_adapter{&actions};
  adapted_parser parser{ts,       grammar.binary, grammar.prefix, grammar.postfix, grammar.ternary,
                        primary,  make_binary,    make_prefix,    make_postfix,    make_ternary,
                        max_depth, grammar.mixfix, &adapter, &paren_adapter};
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
  const size_t base = ts.source_base();
  const size_t last_end = last.offset + last.text.size();
  const size_t limit = base + ts.source().size();
  if (last.kind == token_kind::end || last.offset < first.offset || first.offset < base || last_end > limit ||
      first.offset > limit) {
    return source_span{{}, first.offset, first.line, first.column};
  }
  return source_span{ts.source().substr(first.offset - base, last_end - first.offset), first.offset, first.line,
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
/**
 * @brief Parses `seed suffix*`: the loop behind a left-recursive rule `A := A suffix | seed`.
 *
 * Why: recursive descent cannot call `A` from `A`; the usual fix is to parse the seed and then keep
 * extending the value while a suffix matches. Unlike `parse_chain` the suffixes may have different
 * shapes (`a.b(1)[2]`), and unlike `postfix_op` no precedence table is needed.
 *
 * @param seed `expected<T, parse_error>(token_stream &)`.
 * @param extend `expected<bool, parse_error>(token_stream &, T &left)`. Return `false` (consuming nothing)
 *        when no suffix starts here; otherwise consume the suffix, replace `left` with the extended value
 *        (e.g. `left = make_member(std::move(left), name)`) and return `true`. A malformed suffix returns its
 *        error, which is passed on as is (there is no fall-back to the shorter result).
 * @param max_steps Limit on the number of suffixes; more fails with "too many suffixes". An `extend` that
 *        returns `true` without consuming a token fails with "left recursion made no progress".
 */
template <typename T, typename Seed, typename Extend>
[[nodiscard]] RELOCO_CONSTEXPR20 reloco::expected<T, parse_error>
parse_left_recursive(token_stream &ts, Seed &&seed, Extend &&extend,
                     size_t max_steps = static_cast<size_t>(-1)) noexcept {
  auto first = seed(ts);
  if (!first) {
    return first;
  }
  T value = std::move(first).value();
  size_t steps = 0;
  for (;;) {
    const token before = ts.peek();
    const token consumed_before = ts.previous();
    auto extended = extend(ts, value);
    if (!extended) {
      return reloco::unexpected<parse_error>(extended.error());
    }
    if (!extended.value()) {
      return value;
    }
    if (ts.previous().offset == consumed_before.offset && ts.previous().kind == consumed_before.kind) {
      return reloco::unexpected<parse_error>(parse_error{"left recursion made no progress", before});
    }
    if (steps >= max_steps) {
      return reloco::unexpected<parse_error>(parse_error{"too many suffixes", ts.previous()});
    }
    ++steps;
  }
}

// ============================================================================
// Lexer modes
// ============================================================================

/**
 * @brief Runs `rule` with different lexer rules, then restores the previous ones (also when the rule fails).
 *
 * The lookahead is re-tokenized on entry and on exit, so tokens lexed under the old rules never leak into the
 * region. Nesting works; backtracking works because the stream copy carries the active rules.
 * @param rules rules used inside the region; spans in them must outlive the stream.
 * @param rule `expected<T, parse_error>(token_stream &)` (any non-void result type).
 */
template <typename Rule>
RELOCO_CONSTEXPR20 auto with_rules(token_stream &ts, const lexer_rules &rules, Rule &&rule) noexcept -> decltype(rule(ts)) {
  const lexer_rules saved = lexer_rules(ts.rules());
  ts.set_rules(rules);
  auto result = rule(ts);
  ts.set_rules(saved);
  return result;
}

// ============================================================================
// Memoization (packrat)
// ============================================================================

/**
 * @brief Fixed-capacity, direct-mapped cache of rule results for `memoize`; no heap.
 *
 * An entry is chosen by (rule id, start offset). A collision replaces the older entry, so a small
 * table only costs speed, never correctness. It stores copies of results (and of `parse_error`, whose
 * tokens borrow the parsed source), so it must not outlive the text; call `clear()` between inputs.
 */
template <typename T, size_t N> class RELOCO_POINTER memo_table {
  static_assert(N > 0, "memo_table needs room for at least one entry");
  static_assert(std::is_copy_constructible_v<T>, "memoized results must be copyable; use an index into a node pool");

public:
  /** Lookups answered from the cache. */
  [[nodiscard]] constexpr size_t hits() const noexcept { return hits_; }
  /** Rules that actually ran. */
  [[nodiscard]] constexpr size_t misses() const noexcept { return misses_; }

  /** Forgets every entry and resets the counters. */
  RELOCO_CONSTEXPR20 void clear() noexcept {
    for (size_t i = 0; i < N; ++i) {
      entries_[i] = entry{};
    }
    hits_ = 0;
    misses_ = 0;
  }

private:
  template <typename U, size_t M, typename Rule>
  friend reloco::expected<U, parse_error> memoize(token_stream &, memo_table<U, M> &, uint32_t, Rule &&) noexcept;

  struct entry {
    bool used{false};
    uint32_t rule_id{0};
    size_t offset{0};
    reloco::optional<reloco::expected<T, parse_error>> result{};
    reloco::optional<token_stream> end{};
  };

  [[nodiscard]] static constexpr size_t slot(uint32_t rule_id, size_t offset) noexcept {
    return (offset * 31U + rule_id) % N;
  }

  reloco::array<entry, N> entries_{};
  size_t hits_{0};
  size_t misses_{0};
};

/**
 * @brief Runs `rule(ts)` once per (rule id, position): a hit restores the stream to where the rule ended
 *        and returns the stored result (value or error); a miss runs the rule and stores its outcome.
 *
 * Why: with nested alternatives that share a prefix, `parse_choice` re-parses the same rule at the same
 * position over and over (exponential in the nesting). Remembering the outcome makes the repeat a lookup.
 *
 * The rule must depend only on its position in the input: no external state, no counters, no error lists,
 * no evaluation side effects, because a hit skips them.
 *
 * @param rule_id Small integer, unique per memoized rule sharing the table.
 * @param rule `expected<T, parse_error>(token_stream &)`.
 */
template <typename T, size_t N, typename Rule>
[[nodiscard]] reloco::expected<T, parse_error> memoize(token_stream &ts, memo_table<T, N> &table, uint32_t rule_id,
                                                       Rule &&rule) noexcept {
  const size_t offset = ts.peek().offset;
  auto &slot = table.entries_[memo_table<T, N>::slot(rule_id, offset)];
  if (slot.used && slot.rule_id == rule_id && slot.offset == offset && slot.result && slot.end) {
    ++table.hits_;
    ts = *slot.end;
    return *slot.result;
  }
  ++table.misses_;
  reloco::expected<T, parse_error> result = rule(ts);
  slot.used = true;
  slot.rule_id = rule_id;
  slot.offset = offset;
  slot.result = result;
  slot.end = ts;
  return result;
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
 * memory; check `overflowed()` to tell the user more errors exist. Errors are stored as `stored_error`
 * (static message strings plus positions, no borrowed text), so the list stays valid after the source, or a
 * streaming window, is gone. The offending text is not kept: use the position (`at.offset`, `at.length`).
 */
template <size_t N> class RELOCO_POINTER error_list {
  static_assert(N > 0, "error_list needs room for at least one error");

public:
  /** Stores `error` (detached from the source) while there is room; always counts it. */
  constexpr void add(const parse_error &error) noexcept {
    if (stored_ < N) {
      items_[stored_] = store(error);
      ++stored_;
    }
    ++total_;
  }

  [[nodiscard]] constexpr span<const stored_error> errors() const & noexcept RELOCO_LIFETIMEBOUND {
    return span<const stored_error>(items_.data(), stored_);
  }
  span<const stored_error> errors() const && = delete;

  /** Number of stored errors (at most `N`). */
  [[nodiscard]] constexpr size_t size() const noexcept { return stored_; }
  /** Number of errors added, including those that did not fit. */
  [[nodiscard]] constexpr size_t total() const noexcept { return total_; }
  [[nodiscard]] constexpr bool overflowed() const noexcept { return total_ > stored_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return total_ == 0; }

private:
  reloco::array<stored_error, N> items_{};
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
 * @brief Repeats `item(ts)` while it succeeds and consumes input, handing each value to `collect`.
 *
 * Why: separator-less repetition ("zero or more statements") needs a progress check or an item
 * that consumes nothing loops forever. The caller owns the storage through `collect`, so no heap is used.
 * A failure that consumed nothing ends the loop cleanly (stream restored); a failure after consuming
 * input is a real mistake and is returned. An item that succeeds without consuming ends the loop and
 * its value is discarded.
 *
 * @param item `expected<T, parse_error>(token_stream &)`, `T` not void.
 * @param collect `void(T &&)`; called once per parsed item, in order.
 * @return The number of items parsed.
 */
template <typename Item, typename Sink>
[[nodiscard]] RELOCO_CONSTEXPR20 reloco::expected<size_t, parse_error>
parse_repeat(token_stream &ts, const repeat_options &opt, Item &&item, Sink &&collect) noexcept {
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
    collect(std::move(result).value());
    ++count;
  }
  if (count < opt.min) {
    return reloco::unexpected<parse_error>(parse_error{opt.too_few_message, ts.peek()});
  }
  return count;
}

/**
 * @brief Parses items until the terminator token (left unconsumed), handing each value to `collect`.
 *
 * Why: "statements until `}`" is the common block shape, and here any item failure is a real error
 * (the terminator has not been seen), unlike `parse_repeat`. Reaching the end of input without the
 * terminator is an error, and so is an item that consumes nothing (it would never reach the terminator).
 *
 * @param item `expected<T, parse_error>(token_stream &)`, `T` not void.
 * @param collect `void(T &&)`.
 * @return The number of items parsed.
 */
template <typename Item, typename Sink>
[[nodiscard]] RELOCO_CONSTEXPR20 reloco::expected<size_t, parse_error>
parse_until(token_stream &ts, token_kind kind, microfmt::string_view text, Item &&item, Sink &&collect) noexcept {
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
    collect(std::move(result).value());
    ++count;
  }
  return count;
}

// ============================================================================
// Text that outlives the window (string pool helpers)
// ============================================================================

/**
 * @brief Interns the text of `t` in the stream's string pool and returns its id.
 *
 * Why: a token's `text` points into the source, which a streaming parser discards. The id (4 bytes) stays valid, and
 * equal text gives an equal id. Fails with `no string pool` when the stream was built without one and with
 * `string pool full` at the pool's limits, located at `t`. For a string literal the text includes the quotes; use
 * `expect_string` to get the decoded value.
 */
[[nodiscard]] inline reloco::expected<string_id, parse_error> intern(token_stream &ts, const token &t) noexcept {
  if (!ts.has_strings()) {
    return reloco::unexpected<parse_error>(parse_error{"no string pool", t});
  }
  const pool_result result = ts.strings().intern(t.text);
  if (!result.ok()) {
    return reloco::unexpected<parse_error>(parse_error{result.error, t});
  }
  return result.id;
}

/** Consumes an identifier and returns its interned id; `message` (static) is the error when the next token is not one. */
[[nodiscard]] inline reloco::expected<string_id, parse_error> expect_name(token_stream &ts,
                                                                          microfmt::string_view message) noexcept {
  if (!ts.check(token_kind::identifier)) {
    return reloco::unexpected<parse_error>(ts.error_here(message));
  }
  auto id = intern(ts, ts.peek());
  if (id) {
    ts.next();
  }
  return id;
}

namespace detail {

// The text between the quotes of a string token (a prefix such as `u8` is skipped); text without a quote is returned as is.
[[nodiscard]] inline microfmt::string_view string_body(const lexer_rules &rules, microfmt::string_view text) noexcept {
  size_t open = 0;
  while (open < text.size() && rules.quotes.find(text[open]) == microfmt::string_view::npos &&
         rules.raw_quotes.find(text[open]) == microfmt::string_view::npos &&
         rules.char_quotes.find(text[open]) == microfmt::string_view::npos) {
    ++open;
  }
  if (open >= text.size()) {
    return text;
  }
  const char quote = text[open];
  const size_t from = open + 1;
  const size_t length = text.size() - from;
  if (length > 0 && text[text.size() - 1] == quote) {
    return text.substr(from, length - 1);
  }
  return text.substr(from);
}

} // namespace detail

/**
 * @brief Consumes a string literal, decodes its body with `decoder` and returns the interned value.
 *
 * Why: a literal's source text (`"a\n"`) is not its value (`a` + newline), and neither can be kept after the
 * source is discarded. The decoder (`decode_json_string`, `decode_c_escapes`, `decode_sql_string` or your own
 * `void(string_sink &, string_view raw_body)`) writes the value into the pool. A bad escape is a parse error
 * located at the literal, and nothing is left in the pool. `message` (static) is the error for a non-string token.
 */
[[nodiscard]] inline reloco::expected<string_id, parse_error>
expect_string(token_stream &ts, microfmt::string_view message, string_decoder decoder) noexcept {
  if (!ts.check(token_kind::string)) {
    return reloco::unexpected<parse_error>(ts.error_here(message));
  }
  const token literal = ts.peek();
  if (!ts.has_strings()) {
    return reloco::unexpected<parse_error>(parse_error{"no string pool", literal});
  }
  const pool_result result = ts.strings().decode(detail::string_body(ts.rules(), literal.text), decoder);
  if (!result.ok()) {
    return reloco::unexpected<parse_error>(parse_error{result.error, literal});
  }
  const string_id id = result.id;
  ts.next();
  return id;
}

/** A token whose text lives in the string pool: plain data (no pointers), safe to keep in a node after a discard. */
struct RELOCO_COPYABLE stored_token {
  token_kind kind{token_kind::end};
  string_id text{};
  size_t offset{0};
  uint32_t line{1};
  uint32_t column{1};
};

/** Detaches `t` from the source by interning its text. Fails like `intern`. */
[[nodiscard]] inline reloco::expected<stored_token, parse_error> store(token_stream &ts, const token &t) noexcept {
  auto id = intern(ts, t);
  if (!id) {
    return reloco::unexpected<parse_error>(id.error());
  }
  return stored_token{t.kind, id.value(), t.offset, t.line, t.column};
}

/** The token again, with its text read back from the pool. The text is valid until the pool changes. */
[[nodiscard]] inline token view(const string_pool_ref &strings, const stored_token &stored) noexcept {
  token result;
  result.kind = stored.kind;
  result.text = strings.view(stored.text);
  result.offset = stored.offset;
  result.line = stored.line;
  result.column = stored.column;
  return result;
}

/**
 * @brief Statement boundary for a streaming parse: discard consumed input and reset per-statement state.
 *
 * Does, in order: `ts.discard_consumed()`, `memo.clear()` and `clear()` on every node pool passed in (their
 * handles may point at discarded text). The string pool is NOT cleared, so ids kept from earlier statements stay valid.
 * Call it between statements only, with no `attempt`/`parse_choice` active.
 */
template <typename Memo, typename... Pools>
void end_statement(token_stream &ts, Memo &memo, Pools &...node_pools) noexcept {
  ts.discard_consumed();
  memo.clear();
  (node_pools.clear(), ...);
}

} // namespace microfmt
