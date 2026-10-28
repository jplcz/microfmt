// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

// std-interop-file: the std::string&& constructor overload is deleted to reject dangling temporaries.

/** @file lexer.hpp
 * @brief Allocation-free, rule-table driven lexer (tokenizer) engine.
 *
 * Why: scanning text into identifiers, numbers, strings and operators is
 * needed by command parsers, highlighters and validators; this is the shared,
 * bounds-safe foundation. See `docs/lexer.md` for the user guide and
 * `docs/design/lexer.md` for the design.
 */

#include "../microfmt.hpp"
#include "../reloco.hpp"
#include <cstddef>
#include <cstdint>
#include <reloco/optional.hpp>
#include <string>
#include <type_traits>

namespace microfmt {

// ============================================================================
// Tokens
// ============================================================================

/** Category of a token. `end` is returned forever once the input is exhausted. */
enum class token_kind : uint8_t {
  end,         ///< No more input.
  error,       ///< Unterminated string/comment, malformed number or unknown byte.
  whitespace,  ///< Only produced when `lexer_rules::skip_whitespace` is false.
  newline,     ///< Only produced when `lexer_rules::skip_whitespace` is false.
  identifier,  ///< Name that is not a keyword.
  keyword,     ///< Identifier found in `lexer_rules::keywords`.
  integer,     ///< `42`, `0x2A`, `0b101`, `1_000` (with a digit separator).
  floating,    ///< `1.5`, `1e9`, `.5`.
  string,      ///< Quoted text including both quotes; escapes are kept raw.
  comment,     ///< Only produced when `lexer_rules::skip_comments` is false.
  op,          ///< Longest match from `lexer_rules::operators`.
  punctuation, ///< Single byte from `lexer_rules::punctuation`.
};

/** One token. `text` borrows from the lexed source and must not outlive it. */
struct RELOCO_POINTER token {
  token_kind kind{token_kind::end};
  microfmt::string_view text{};
  size_t offset{0};   ///< Byte offset of `text` in the source.
  uint32_t line{1};   ///< 1-based line.
  uint32_t column{1}; ///< 1-based column, counted in bytes.
};

// ============================================================================
// Sorted word lists
// ============================================================================

/**
 * @brief Non-owning view of a sorted, duplicate-free word list.
 *
 * Why: keywords are binary-searched, so order matters. The only ways to get a
 * non-empty instance are `from_sorted` / `try_from_sorted` (which verify the
 * order) or a `keyword_table` (which sorts itself), so an unsorted list can
 * never reach the lexer.
 */
class RELOCO_POINTER sorted_words {
public:
  constexpr sorted_words() noexcept = default;

  /**
   * @brief Wraps already-sorted storage after verifying it (checked tier).
   * @param words Words in strictly ascending byte order; the storage must outlive the result.
   * A violation is a compile error in a constant expression and a RELOCO_ASSERT failure otherwise.
   */
  [[nodiscard]] static constexpr sorted_words from_sorted(span<const microfmt::string_view> words
                                                              RELOCO_LIFETIMEBOUND) noexcept {
    RELOCO_ASSERT(is_strictly_sorted(words), "sorted_words::from_sorted requires strictly ascending words");
    return sorted_words(words);
  }

  /** try_ tier: like `from_sorted` but returns an empty optional for unsorted or duplicate input (constexpr from C++20, since `reloco::optional` is not a literal type before). */
  [[nodiscard]] static RELOCO_CONSTEXPR20 reloco::optional<sorted_words>
  try_from_sorted(span<const microfmt::string_view> words RELOCO_LIFETIMEBOUND) noexcept {
    if (!is_strictly_sorted(words)) {
      return reloco::optional<sorted_words>{};
    }
    return reloco::optional<sorted_words>{sorted_words(words)};
  }

  /** Binary search; O(log size). */
  [[nodiscard]] constexpr bool contains(microfmt::string_view word) const noexcept {
    size_t low = 0;
    size_t high = words_.size();
    while (low < high) {
      const size_t mid = low + (high - low) / 2;
      const microfmt::string_view candidate = words_[mid];
      if (candidate == word) {
        return true;
      }
      if (candidate < word) {
        low = mid + 1;
      } else {
        high = mid;
      }
    }
    return false;
  }

  [[nodiscard]] constexpr size_t size() const noexcept { return words_.size(); }

private:
  constexpr explicit sorted_words(span<const microfmt::string_view> words) noexcept : words_(words) {}

  [[nodiscard]] static constexpr bool is_strictly_sorted(span<const microfmt::string_view> words) noexcept {
    for (size_t i = 1; i < words.size(); ++i) {
      if (!(words[i - 1] < words[i])) {
        return false;
      }
    }
    return true;
  }

  span<const microfmt::string_view> words_{};

  template <size_t N> friend class keyword_table;
};

/**
 * @brief Owning, fixed-size, always sorted word table that sorts itself when built.
 *
 * Why: users write keywords in any order (`keyword_table{"let", "if"}`) and still
 * get the sorted storage the lexer needs. Duplicates are a compile error in a
 * constant expression and a RELOCO_ASSERT failure at runtime.
 */
template <size_t N> class keyword_table {
  static_assert(N > 0, "keyword_table needs at least one word");

public:
  template <typename... Words, std::enable_if_t<sizeof...(Words) == N, int> = 0>
  explicit constexpr keyword_table(Words... words) noexcept : words_{{microfmt::string_view(words)...}} {
    // Insertion sort: N is small and this stays constexpr and allocation-free.
    for (size_t i = 1; i < N; ++i) {
      const microfmt::string_view key = words_[i];
      size_t j = i;
      while (j > 0 && key < words_[j - 1]) {
        words_[j] = words_[j - 1];
        --j;
      }
      words_[j] = key;
    }
    for (size_t i = 1; i < N; ++i) {
      RELOCO_ASSERT(words_[i - 1] < words_[i], "keyword_table contains a duplicate word");
    }
  }

  [[nodiscard]] constexpr sorted_words view() const & noexcept RELOCO_LIFETIMEBOUND {
    return sorted_words(span<const microfmt::string_view>(words_.data(), N));
  }
  sorted_words view() const && = delete;

  // Implicit on purpose so `lexer_rules::keywords = table;` reads naturally.
  constexpr operator sorted_words() const & noexcept RELOCO_LIFETIMEBOUND { return view(); }
  operator sorted_words() const && = delete;

private:
  reloco::array<microfmt::string_view, N> words_;
};

template <typename... Words> keyword_table(Words...) -> keyword_table<sizeof...(Words)>;

/** Builds a keyword table; in C++20 always evaluated at compile time so a duplicate cannot reach runtime. */
template <typename... Words>
#if RELOCO_CXX20
[[nodiscard]] consteval
#else
[[nodiscard]] constexpr
#endif
    keyword_table<sizeof...(Words)>
    make_keyword_table(Words... words) noexcept {
  return keyword_table<sizeof...(Words)>(words...);
}

// ============================================================================
// Rules
// ============================================================================

/**
 * @brief Language description: every member is a view of static data or a flag.
 *
 * Every rule can be disabled by leaving its member empty/false, see
 * `docs/lexer.md` for the full list.
 */
struct lexer_rules {
  sorted_words keywords{};                           ///< Exact, case-sensitive.
  span<const microfmt::string_view> operators{};     ///< Longest match wins; order irrelevant.
  microfmt::string_view punctuation{"(){}[],;"};     ///< Single-byte tokens.
  microfmt::string_view line_comment{};              ///< e.g. `//` or `#`; empty = none.
  microfmt::string_view block_comment_open{};        ///< e.g. `/*`; empty = none.
  microfmt::string_view block_comment_close{};       ///< e.g. `*/`.
  bool nested_block_comments{false};                 ///< `/* a /* b */ c */` is one comment.
  microfmt::string_view identifier_start_extra{};    ///< Extra identifier-start bytes, e.g. `$@`.
  microfmt::string_view identifier_continue_extra{}; ///< Extra identifier bytes after the first, e.g. `-.`.
  microfmt::string_view quotes{"\"'"};               ///< Each byte opens a string ended by the same byte.
  char escape{'\\'};                                 ///< Escape byte inside strings; `\0` disables escapes.
  char digit_separator{'_'};                         ///< Allowed between digits; `\0` disables it.
  bool hex_binary_prefixes{true};                    ///< Recognise `0x..` and `0b..`.
  bool skip_whitespace{true};
  bool skip_comments{true};
};

/** Checks that the rule fields are mutually consistent (comment markers paired, extras sane). */
[[nodiscard]] constexpr bool lexer_rules_valid(const lexer_rules &rules) noexcept {
  const bool has_open = !rules.block_comment_open.empty();
  const bool has_close = !rules.block_comment_close.empty();
  if (has_open != has_close) {
    return false;
  }
  if (rules.nested_block_comments && !has_open) {
    return false;
  }
  for (size_t i = 0; i < rules.identifier_start_extra.size(); ++i) {
    const char ch = rules.identifier_start_extra[i];
    if ((ch >= '0' && ch <= '9') || ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
      return false;
    }
  }
  for (size_t i = 0; i < rules.identifier_continue_extra.size(); ++i) {
    const char ch = rules.identifier_continue_extra[i];
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
      return false;
    }
  }
  return true;
}

// ============================================================================
// Lexer
// ============================================================================

/**
 * @brief Cheap cursor producing tokens from one contiguous text.
 *
 * Every `next()` call consumes at least one byte unless it returns `end`, so a
 * loop over `next()` always terminates within `size + 1` calls.
 */
class RELOCO_POINTER lexer {
public:
  /** @param source Text to tokenize; must outlive the lexer. @param rules Language description (copied). */
  explicit constexpr lexer(microfmt::string_view source RELOCO_LIFETIMEBOUND, const lexer_rules &rules) noexcept
      : source_(source), rules_(rules) {}
  // Only an rvalue std::string is rejected; literals and lvalues still bind to string_view above.
  template <typename S, std::enable_if_t<std::is_same_v<S, std::string>, int> = 0>
  explicit lexer(S &&, const lexer_rules &) = delete;

  /** Consumes and returns the next token. */
  [[nodiscard]] constexpr token next() noexcept {
    const token result = scan(cursor_);
    if (result.kind == token_kind::end) {
      ended_ = true;
    }
    return result;
  }

  /** Returns the next token without consuming it. */
  [[nodiscard]] constexpr token peek() const noexcept {
    cursor scratch = cursor_;
    return scan(scratch);
  }

  /** Rewinds to the start of the source. */
  constexpr void reset() noexcept {
    cursor_ = cursor{};
    ended_ = false;
  }

  /** True once `next()` has returned `token_kind::end`. */
  [[nodiscard]] constexpr bool at_end() const noexcept { return ended_; }

private:
  struct cursor {
    size_t pos{0};
    uint32_t line{1};
    uint32_t column{1};
  };

  [[nodiscard]] constexpr char at(size_t index) const noexcept {
    return index < source_.size() ? source_[index] : '\0';
  }

  [[nodiscard]] constexpr bool starts_with_at(size_t index, microfmt::string_view prefix) const noexcept {
    return !prefix.empty() && index <= source_.size() && source_.substr(index).starts_with(prefix);
  }

  [[nodiscard]] static constexpr bool is_digit(char ch) noexcept { return ch >= '0' && ch <= '9'; }

  [[nodiscard]] static constexpr bool is_alpha(char ch) noexcept {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
  }

  [[nodiscard]] static constexpr bool contains_byte(microfmt::string_view set, char ch) noexcept {
    for (size_t i = 0; i < set.size(); ++i) {
      if (set[i] == ch) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] static constexpr bool is_high_byte(char ch) noexcept { return (static_cast<unsigned char>(ch) & 0x80U) != 0U; }

  [[nodiscard]] constexpr bool is_ident_start(char ch) const noexcept {
    return is_alpha(ch) || ch == '_' || is_high_byte(ch) || contains_byte(rules_.identifier_start_extra, ch);
  }

  [[nodiscard]] constexpr bool is_ident_continue(char ch) const noexcept {
    return is_ident_start(ch) || is_digit(ch) || contains_byte(rules_.identifier_continue_extra, ch);
  }

  [[nodiscard]] static constexpr bool is_hex_digit(char ch) noexcept {
    return is_digit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
  }

  // Moves the cursor forward to `target`, keeping line/column in sync.
  constexpr void advance_to(cursor &c, size_t target) const noexcept {
    const size_t limit = target < source_.size() ? target : source_.size();
    for (size_t i = c.pos; i < limit; ++i) {
      if (source_[i] == '\n') {
        ++c.line;
        c.column = 1;
      } else {
        ++c.column;
      }
    }
    c.pos = limit;
  }

  [[nodiscard]] constexpr token emit(cursor &c, token_kind kind, size_t length) const noexcept {
    token result;
    result.kind = kind;
    result.text = source_.substr(c.pos, length);
    result.offset = c.pos;
    result.line = c.line;
    result.column = c.column;
    advance_to(c, c.pos + length);
    return result;
  }

  // Length of the newline at `index` (0 when there is none); a lone '\r' is whitespace.
  [[nodiscard]] constexpr size_t newline_length(size_t index) const noexcept {
    if (at(index) == '\n') {
      return 1;
    }
    if (at(index) == '\r' && at(index + 1) == '\n') {
      return 2;
    }
    return 0;
  }

  [[nodiscard]] constexpr size_t scan_whitespace(size_t index) const noexcept {
    size_t i = index;
    while (i < source_.size() && newline_length(i) == 0) {
      const char ch = source_[i];
      if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\v' && ch != '\f') {
        break;
      }
      ++i;
    }
    return i - index;
  }

  [[nodiscard]] constexpr size_t scan_line_comment(size_t index) const noexcept {
    size_t i = index;
    while (i < source_.size() && newline_length(i) == 0) {
      ++i;
    }
    return i - index;
  }

  // Returns the comment length, or 0 when unterminated (caller turns that into an error).
  [[nodiscard]] constexpr size_t scan_block_comment(size_t index, bool &terminated) const noexcept {
    const size_t open_size = rules_.block_comment_open.size();
    const size_t close_size = rules_.block_comment_close.size();
    size_t depth = 1;
    size_t i = index + open_size;
    while (i < source_.size()) {
      if (rules_.nested_block_comments && starts_with_at(i, rules_.block_comment_open)) {
        ++depth;
        i += open_size;
      } else if (starts_with_at(i, rules_.block_comment_close)) {
        --depth;
        i += close_size;
        if (depth == 0) {
          terminated = true;
          return i - index;
        }
      } else {
        ++i;
      }
    }
    terminated = false;
    return source_.size() - index;
  }

  [[nodiscard]] constexpr size_t scan_string(size_t index, bool &terminated) const noexcept {
    const char quote = source_[index];
    size_t i = index + 1;
    terminated = false;
    while (i < source_.size()) {
      const char ch = source_[i];
      if (rules_.escape != '\0' && ch == rules_.escape) {
        i += 2;
        continue;
      }
      if (ch == quote) {
        ++i;
        terminated = true;
        break;
      }
      if (ch == '\n') {
        break;
      }
      ++i;
    }
    if (i > source_.size()) {
      i = source_.size();
    }
    return i - index;
  }

  // Consumes digits (and separators between digits) accepted by `accept`.
  template <typename Pred> [[nodiscard]] constexpr size_t scan_digits(size_t index, Pred accept) const noexcept {
    size_t i = index;
    while (i < source_.size()) {
      const char ch = source_[i];
      if (accept(ch)) {
        ++i;
      } else if (rules_.digit_separator != '\0' && ch == rules_.digit_separator && i > index && accept(at(i + 1))) {
        ++i;
      } else {
        break;
      }
    }
    return i - index;
  }

  [[nodiscard]] constexpr token scan_number(cursor &c) const noexcept {
    const size_t start = c.pos;
    size_t i = start;
    token_kind kind = token_kind::integer;
    bool malformed = false;

    const char second = at(start + 1);
    if (rules_.hex_binary_prefixes && at(start) == '0' && (second == 'x' || second == 'X')) {
      i += 2;
      const size_t digits = scan_digits(i, is_hex_digit);
      malformed = digits == 0;
      i += digits;
    } else if (rules_.hex_binary_prefixes && at(start) == '0' && (second == 'b' || second == 'B')) {
      i += 2;
      const size_t digits = scan_digits(i, [](char ch) { return ch == '0' || ch == '1'; });
      malformed = digits == 0;
      i += digits;
    } else {
      i += scan_digits(i, is_digit);
      if (at(i) == '.' && is_digit(at(i + 1))) {
        kind = token_kind::floating;
        i += 1 + scan_digits(i + 1, is_digit);
      }
      const char e = at(i);
      if (e == 'e' || e == 'E') {
        const char sign = at(i + 1);
        const size_t digit_at = (sign == '+' || sign == '-') ? i + 2 : i + 1;
        if (is_digit(at(digit_at))) {
          kind = token_kind::floating;
          i = digit_at + scan_digits(digit_at, is_digit);
        }
      }
    }

    // A number glued to identifier characters (`12ab`) is malformed as a whole.
    if (i < source_.size() && is_ident_continue(source_[i])) {
      malformed = true;
      while (i < source_.size() && is_ident_continue(source_[i])) {
        ++i;
      }
    }
    return emit(c, malformed ? token_kind::error : kind, i - start);
  }

  [[nodiscard]] constexpr size_t longest_operator(size_t index) const noexcept {
    size_t best = 0;
    for (size_t i = 0; i < rules_.operators.size(); ++i) {
      const microfmt::string_view candidate = rules_.operators[i];
      if (candidate.size() > best && starts_with_at(index, candidate)) {
        best = candidate.size();
      }
    }
    return best;
  }

  [[nodiscard]] constexpr token scan(cursor &c) const noexcept {
    for (;;) {
      if (c.pos >= source_.size()) {
        token result;
        result.offset = source_.size();
        result.line = c.line;
        result.column = c.column;
        return result;
      }

      const char ch = source_[c.pos];

      if (const size_t nl = newline_length(c.pos); nl != 0) {
        if (rules_.skip_whitespace) {
          advance_to(c, c.pos + nl);
          continue;
        }
        return emit(c, token_kind::newline, nl);
      }
      if (const size_t ws = scan_whitespace(c.pos); ws != 0) {
        if (rules_.skip_whitespace) {
          advance_to(c, c.pos + ws);
          continue;
        }
        return emit(c, token_kind::whitespace, ws);
      }

      if (starts_with_at(c.pos, rules_.line_comment)) {
        const size_t length = scan_line_comment(c.pos);
        if (rules_.skip_comments) {
          advance_to(c, c.pos + length);
          continue;
        }
        return emit(c, token_kind::comment, length);
      }
      if (starts_with_at(c.pos, rules_.block_comment_open)) {
        bool terminated = false;
        const size_t length = scan_block_comment(c.pos, terminated);
        if (!terminated) {
          return emit(c, token_kind::error, length);
        }
        if (rules_.skip_comments) {
          advance_to(c, c.pos + length);
          continue;
        }
        return emit(c, token_kind::comment, length);
      }

      if (contains_byte(rules_.quotes, ch)) {
        bool terminated = false;
        const size_t length = scan_string(c.pos, terminated);
        return emit(c, terminated ? token_kind::string : token_kind::error, length);
      }

      if (is_digit(ch) || (ch == '.' && is_digit(at(c.pos + 1)))) {
        return scan_number(c);
      }

      if (is_ident_start(ch)) {
        size_t i = c.pos + 1;
        while (i < source_.size() && is_ident_continue(source_[i])) {
          ++i;
        }
        const size_t length = i - c.pos;
        const bool is_keyword = rules_.keywords.contains(source_.substr(c.pos, length));
        return emit(c, is_keyword ? token_kind::keyword : token_kind::identifier, length);
      }

      if (const size_t op = longest_operator(c.pos); op != 0) {
        return emit(c, token_kind::op, op);
      }
      if (contains_byte(rules_.punctuation, ch)) {
        return emit(c, token_kind::punctuation, 1);
      }
      return emit(c, token_kind::error, 1);
    }
  }

  microfmt::string_view source_;
  lexer_rules rules_;
  cursor cursor_{};
  bool ended_{false};
};

} // namespace microfmt
