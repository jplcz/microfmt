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
 * bounds-safe foundation. See `docs/lexer.md` for the user guide.
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
  character,   ///< Character literal from `lexer_rules::char_quotes`, prefix and quotes included.
  directive,   ///< Whole line starting with `lexer_rules::directive_prefix` (C preprocessor).
  datetime,    ///< TOML-style date/time, only with `lexer_rules::datetime_literals`.
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

  /** Binary search comparing `word` with ASCII letters folded to lower case; the list must hold lower-case words. */
  [[nodiscard]] constexpr bool contains_ignore_case(microfmt::string_view word) const noexcept {
    size_t low = 0;
    size_t high = words_.size();
    while (low < high) {
      const size_t mid = low + (high - low) / 2;
      const int order = compare_folded(words_[mid], word);
      if (order == 0) {
        return true;
      }
      if (order < 0) {
        low = mid + 1;
      } else {
        high = mid;
      }
    }
    return false;
  }

  /** True when no word contains an ASCII upper-case letter (required for `contains_ignore_case`). */
  [[nodiscard]] constexpr bool is_ascii_lower_case() const noexcept {
    for (size_t i = 0; i < words_.size(); ++i) {
      for (size_t j = 0; j < words_[i].size(); ++j) {
        if (words_[i][j] >= 'A' && words_[i][j] <= 'Z') {
          return false;
        }
      }
    }
    return true;
  }

  [[nodiscard]] constexpr size_t size() const noexcept { return words_.size(); }

private:
  [[nodiscard]] static constexpr char fold(char ch) noexcept {
    return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
  }

  // Orders `stored` against `word` as if `word` were lower-case: <0 when stored sorts first.
  [[nodiscard]] static constexpr int compare_folded(microfmt::string_view stored, microfmt::string_view word) noexcept {
    const size_t common = stored.size() < word.size() ? stored.size() : word.size();
    for (size_t i = 0; i < common; ++i) {
      const auto a = static_cast<unsigned char>(stored[i]);
      const auto b = static_cast<unsigned char>(fold(word[i]));
      if (a != b) {
        return a < b ? -1 : 1;
      }
    }
    if (stored.size() == word.size()) {
      return 0;
    }
    return stored.size() < word.size() ? -1 : 1;
  }

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

  // --- Language extensions, all off by default ---
  span<const microfmt::string_view> line_comments{};   ///< Extra line-comment markers (INI: `;` next to `#`).
  bool line_continuation{false};                       ///< Backslash + newline continues line comments and directives.
  bool long_bracket_comments{false};                   ///< Lua: a line-comment marker followed by `[==[` opens a long comment.
  microfmt::string_view char_quotes{};                 ///< Quote bytes opening a character literal (`'` in C).
  microfmt::string_view raw_quotes{};                  ///< Quote bytes whose strings have no escapes (TOML `'`).
  bool doubled_quote_escape{false};                    ///< SQL: a doubled quote inside a string is a literal quote.
  microfmt::string_view multiline_triple_quotes{};     ///< Quote bytes allowing triple-quoted multi-line strings.
  microfmt::string_view multiline_quotes{};            ///< Quote bytes whose strings may contain raw newlines.
  bool long_bracket_strings{false};                    ///< Lua: `[[ ... ]]` and `[==[ ... ]==]`.
  span<const microfmt::string_view> string_prefixes{}; ///< `u8`, `L`, `R`, ... directly before a quote.
  bool cpp_raw_strings{false};                         ///< A prefix ending in `R` starts `R"delim( ... )delim"`.
  span<const microfmt::string_view> number_suffixes{}; ///< `u`, `ul`, `f`, ...; ASCII case-insensitive, longest wins.
  bool user_defined_suffixes{false};                   ///< C++ UDLs: an identifier run after a number/string/char joins the token.
  bool hex_float{false};                               ///< `0x1.8p3`.
  bool decimal_trailing_dot{false};                    ///< `4.` is a float (not before a second `.`: `1..2`).
  bool octal_prefix{false};                            ///< `0o17`.
  bool numbers{true};                                  ///< false: digits start identifiers (INI/config values are bare words).
  bool datetime_literals{false};                       ///< `1979-05-27`, `07:32:00`, `1979-05-27T07:32:00Z`.
  bool keywords_ignore_case{false};                    ///< ASCII fold on lookup; the keyword table must be lower-case.
  char directive_prefix{'\0'};                         ///< `#` makes a first-on-line `#...` a `directive` token.
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
  if (rules.long_bracket_comments && rules.line_comment.empty() && rules.line_comments.empty()) {
    return false;
  }
  if (rules.keywords_ignore_case && !rules.keywords.is_ascii_lower_case()) {
    return false;
  }
  for (size_t i = 0; i < rules.char_quotes.size(); ++i) {
    for (size_t j = 0; j < rules.quotes.size(); ++j) {
      if (rules.char_quotes[i] == rules.quotes[j]) {
        return false;
      }
    }
  }
  bool has_raw_prefix = false;
  for (size_t i = 0; i < rules.string_prefixes.size(); ++i) {
    if (rules.string_prefixes[i].empty()) {
      return false;
    }
    has_raw_prefix = has_raw_prefix || rules.string_prefixes[i].back() == 'R';
  }
  if (rules.cpp_raw_strings && !has_raw_prefix) {
    return false;
  }
  for (size_t i = 0; i < rules.number_suffixes.size(); ++i) {
    if (rules.number_suffixes[i].empty()) {
      return false;
    }
  }
  for (size_t i = 0; i < rules.line_comments.size(); ++i) {
    if (rules.line_comments[i].empty()) {
      return false;
    }
    if (rules.directive_prefix != '\0' && rules.line_comments[i].front() == rules.directive_prefix) {
      return false;
    }
  }
  if (rules.directive_prefix != '\0' && !rules.line_comment.empty() &&
      rules.line_comment.front() == rules.directive_prefix) {
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

  /** A resumable place in the source: byte offset plus its 1-based line and column. */
  struct RELOCO_COPYABLE position {
    size_t pos{0};
    uint32_t line{1};
    uint32_t column{1};
  };

  /** The place the next `next()` call starts scanning from. */
  [[nodiscard]] constexpr position state() const noexcept { return cursor_; }

  /** Continues scanning from `where` (a value returned by `state()` or `position_after()`). */
  constexpr void restore(position where) noexcept {
    cursor_ = where;
    ended_ = false;
  }

  /** The place directly after `tok`, which must come from this lexer's source. */
  [[nodiscard]] constexpr position position_after(const token &tok) const noexcept {
    position where{tok.offset, tok.line, tok.column};
    advance_to(where, tok.offset + tok.text.size());
    return where;
  }

  /** The active rules. */
  [[nodiscard]] constexpr const lexer_rules &rules() const noexcept RELOCO_LIFETIMEBOUND { return rules_; }

  /**
   * Points the lexer at a different view of the same text (a streaming window that grew). Offsets stay
   * relative to the first byte of the view; the cursor is kept.
   */
  constexpr void set_source(microfmt::string_view source) noexcept {
    source_ = source;
    ended_ = false;
  }

  /** Replaces the rules used from the next scan on; already returned tokens are not affected. */
  constexpr void set_rules(const lexer_rules &rules) noexcept { rules_ = lexer_rules(rules); }

  /**
   * Returns the bytes from the current place up to (not including) the first byte of `stop_bytes`, or to the end
   * of the source, as a `string` token without quotes. Whitespace is kept. The run may be empty.
   */
  [[nodiscard]] constexpr token take_raw(microfmt::string_view stop_bytes) noexcept {
    size_t i = cursor_.pos;
    while (i < source_.size() && !contains_byte(stop_bytes, source_[i])) {
      ++i;
    }
    return emit(cursor_, token_kind::string, i - cursor_.pos);
  }

private:
  using cursor = position;

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
    return set.find(ch) != microfmt::string_view::npos;
  }

  [[nodiscard]] static constexpr bool is_high_byte(char ch) noexcept { return (static_cast<unsigned char>(ch) & 0x80U) != 0U; }

  [[nodiscard]] constexpr bool is_ident_start(char ch) const noexcept {
    return is_alpha(ch) || ch == '_' || is_high_byte(ch) || contains_byte(rules_.identifier_start_extra, ch) ||
           (!rules_.numbers && is_digit(ch));
  }

  // Letters, digits, `_` and bytes >= 0x80 only; the language-specific extras (`-`, `.`) do not count, so
  // `2024-key` lexes as number, `-`, identifier instead of one malformed number.
  [[nodiscard]] static constexpr bool is_base_ident_continue(char ch) noexcept {
    return is_alpha(ch) || is_digit(ch) || ch == '_' || is_high_byte(ch);
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

  // Length of the backslash-newline at `index` (0 when none); only honoured with `line_continuation`.
  [[nodiscard]] constexpr size_t continuation_length(size_t index) const noexcept {
    if (rules_.line_continuation && at(index) == '\\') {
      return newline_length(index + 1) == 0 ? 0 : 1 + newline_length(index + 1);
    }
    return 0;
  }

  // Scans to the end of the line, hopping over continued lines.
  [[nodiscard]] constexpr size_t scan_line_comment(size_t index) const noexcept {
    size_t i = index;
    while (i < source_.size() && newline_length(i) == 0) {
      if (const size_t cont = continuation_length(i); cont != 0) {
        i += cont;
      } else {
        ++i;
      }
    }
    return i - index;
  }

  // Size of the line-comment marker starting at `index` (0 when none); the longest marker wins.
  [[nodiscard]] constexpr size_t line_comment_marker(size_t index) const noexcept {
    size_t best = starts_with_at(index, rules_.line_comment) ? rules_.line_comment.size() : 0;
    for (size_t i = 0; i < rules_.line_comments.size(); ++i) {
      const microfmt::string_view marker = rules_.line_comments[i];
      if (marker.size() > best && starts_with_at(index, marker)) {
        best = marker.size();
      }
    }
    return best;
  }

  // True when only blanks precede `index` on its line.
  [[nodiscard]] constexpr bool first_on_line(size_t index) const noexcept {
    size_t j = index;
    while (j > 0 && (source_[j - 1] == ' ' || source_[j - 1] == '\t' || source_[j - 1] == '\v' || source_[j - 1] == '\f')) {
      --j;
    }
    return j == 0 || source_[j - 1] == '\n';
  }

  // Length of a long-bracket opener `[` `=`* `[` at `index` (0 when none); `level` receives the `=` count.
  [[nodiscard]] constexpr size_t long_bracket_open(size_t index, size_t &level) const noexcept {
    constexpr size_t max_level = 255;
    if (at(index) != '[') {
      return 0;
    }
    size_t equals = 0;
    while (at(index + 1 + equals) == '=' && equals <= max_level) {
      ++equals;
    }
    if (equals > max_level || at(index + 1 + equals) != '[') {
      return 0;
    }
    level = equals;
    return equals + 2;
  }

  // Scans a long bracket whose opener starts at `index`; unterminated runs to the end of the input.
  [[nodiscard]] constexpr size_t scan_long_bracket(size_t index, size_t opener, size_t level,
                                                   bool &terminated) const noexcept {
    size_t i = index + opener;
    while (i < source_.size()) {
      if (source_[i] == ']') {
        size_t equals = 0;
        while (equals < level && at(i + 1 + equals) == '=') {
          ++equals;
        }
        if (equals == level && at(i + 1 + level) == ']') {
          terminated = true;
          return i + level + 2 - index;
        }
      }
      ++i;
    }
    terminated = false;
    return source_.size() - index;
  }

  // A directive runs to the end of the line, hopping over continued lines.
  [[nodiscard]] constexpr size_t scan_directive(size_t index) const noexcept { return scan_line_comment(index); }

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

  // Scans a quoted literal whose opening quote is at `index`; honours raw/triple/multiline/doubled-quote rules.
  [[nodiscard]] constexpr size_t scan_quoted(size_t index, bool &terminated) const noexcept {
    const char quote = source_[index];
    const bool raw = contains_byte(rules_.raw_quotes, quote);
    const bool escapes = !raw && rules_.escape != '\0';
    const bool triple = contains_byte(rules_.multiline_triple_quotes, quote) && at(index + 1) == quote &&
                        at(index + 2) == quote;
    const bool multiline = triple || contains_byte(rules_.multiline_quotes, quote);
    size_t i = index + (triple ? 3 : 1);
    terminated = false;
    while (i < source_.size()) {
      const char ch = source_[i];
      if (escapes && ch == rules_.escape) {
        i += 2;
        continue;
      }
      if (ch == quote) {
        if (triple) {
          if (at(i + 1) == quote && at(i + 2) == quote) {
            i += 3;
            terminated = true;
            break;
          }
        } else if (rules_.doubled_quote_escape && at(i + 1) == quote) {
          i += 2;
          continue;
        } else {
          ++i;
          terminated = true;
          break;
        }
      } else if (ch == '\n' && !multiline) {
        break;
      }
      ++i;
    }
    if (i > source_.size()) {
      i = source_.size();
    }
    return i - index;
  }

  // C++ raw string `"delim( ... )delim"` whose opening quote is at `quote_index`; returns the length from there.
  [[nodiscard]] constexpr size_t scan_raw_string(size_t quote_index, bool &terminated) const noexcept {
    constexpr size_t max_delimiter = 16;
    size_t delimiter = 0;
    while (delimiter <= max_delimiter && at(quote_index + 1 + delimiter) != '(') {
      const char ch = at(quote_index + 1 + delimiter);
      if (quote_index + 1 + delimiter >= source_.size() || ch == ' ' || ch == ')' || ch == '\\' || ch == '\t' ||
          ch == '\v' || ch == '\f' || ch == '\n' || ch == '\r' || ch == '"') {
        terminated = false;
        return 1; // malformed opener: only the quote becomes an error token
      }
      ++delimiter;
    }
    if (delimiter > max_delimiter) {
      terminated = false;
      return 1;
    }
    const size_t body = quote_index + 2 + delimiter;
    for (size_t i = body; i < source_.size(); ++i) {
      if (source_[i] == ')' && at(i + 1 + delimiter) == '"' &&
          source_.substr(i + 1, delimiter) == source_.substr(quote_index + 1, delimiter)) {
        terminated = true;
        return i + delimiter + 2 - quote_index;
      }
    }
    terminated = false;
    return source_.size() - quote_index;
  }

  // Length of the longest `string_prefixes` entry at `index` that is directly followed by a quote (0 when none).
  [[nodiscard]] constexpr size_t string_prefix_length(size_t index) const noexcept {
    size_t best = 0;
    for (size_t i = 0; i < rules_.string_prefixes.size(); ++i) {
      const microfmt::string_view prefix = rules_.string_prefixes[i];
      const char next = at(index + prefix.size());
      if (prefix.size() > best && starts_with_at(index, prefix) &&
          (contains_byte(rules_.quotes, next) || contains_byte(rules_.char_quotes, next))) {
        best = prefix.size();
      }
    }
    return best;
  }

  [[nodiscard]] constexpr token scan_quote_like(cursor &c, size_t prefix) const noexcept {
    const size_t quote_index = c.pos + prefix;
    const char quote = source_[quote_index];
    bool terminated = false;
    size_t length = 0;
    if (rules_.cpp_raw_strings && prefix != 0 && source_[quote_index - 1] == 'R' && contains_byte(rules_.quotes, quote)) {
      length = scan_raw_string(quote_index, terminated);
    } else {
      length = scan_quoted(quote_index, terminated);
    }
    if (!terminated) {
      return emit(c, token_kind::error, prefix + length);
    }
    size_t total = prefix + length;
    if (rules_.user_defined_suffixes) {
      while (c.pos + total < source_.size() && is_ident_continue(source_[c.pos + total])) {
        ++total;
      }
    }
    return emit(c, contains_byte(rules_.char_quotes, quote) ? token_kind::character : token_kind::string, total);
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

  [[nodiscard]] static constexpr char fold_lower(char ch) noexcept {
    return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
  }

  // Length of the longest `number_suffixes` entry at `index` (ASCII case-insensitive) not followed by an identifier byte.
  [[nodiscard]] constexpr size_t number_suffix_length(size_t index) const noexcept {
    size_t best = 0;
    for (size_t i = 0; i < rules_.number_suffixes.size(); ++i) {
      const microfmt::string_view suffix = rules_.number_suffixes[i];
      if (suffix.size() <= best || index + suffix.size() > source_.size() || is_base_ident_continue(at(index + suffix.size()))) {
        continue;
      }
      bool same = true;
      for (size_t j = 0; j < suffix.size() && same; ++j) {
        same = fold_lower(source_[index + j]) == fold_lower(suffix[j]);
      }
      if (same) {
        best = suffix.size();
      }
    }
    return best;
  }

  // Length of `dd:dd:dd[.d+][Z|+dd:dd|-dd:dd]` at `index` (0 when it does not match).
  [[nodiscard]] constexpr size_t time_length(size_t index) const noexcept {
    if (!(is_digit(at(index)) && is_digit(at(index + 1)) && at(index + 2) == ':' && is_digit(at(index + 3)) &&
          is_digit(at(index + 4)) && at(index + 5) == ':' && is_digit(at(index + 6)) && is_digit(at(index + 7)))) {
      return 0;
    }
    size_t i = index + 8;
    if (at(i) == '.' && is_digit(at(i + 1))) {
      i += 1 + scan_digits_plain(i + 1);
    }
    if (at(i) == 'Z' || at(i) == 'z') {
      ++i;
    } else if ((at(i) == '+' || at(i) == '-') && is_digit(at(i + 1)) && is_digit(at(i + 2)) && at(i + 3) == ':' &&
               is_digit(at(i + 4)) && is_digit(at(i + 5))) {
      i += 6;
    }
    return i - index;
  }

  [[nodiscard]] constexpr size_t scan_digits_plain(size_t index) const noexcept {
    size_t i = index;
    while (is_digit(at(i))) {
      ++i;
    }
    return i - index;
  }

  // Length of a date, time or date-time at `index` (0 when none). Field ranges are left to the parser.
  [[nodiscard]] constexpr size_t datetime_length(size_t index) const noexcept {
    size_t i = index;
    const bool date = is_digit(at(i)) && is_digit(at(i + 1)) && is_digit(at(i + 2)) && is_digit(at(i + 3)) &&
                      at(i + 4) == '-' && is_digit(at(i + 5)) && is_digit(at(i + 6)) && at(i + 7) == '-' &&
                      is_digit(at(i + 8)) && is_digit(at(i + 9));
    if (date) {
      i += 10;
      const char sep = at(i);
      if (sep == 'T' || sep == 't' || sep == ' ') {
        if (const size_t time = time_length(i + 1); time != 0) {
          i += 1 + time;
        }
      }
    } else if (const size_t time = time_length(i); time != 0) {
      i += time;
    } else {
      return 0;
    }
    return is_base_ident_continue(at(i)) ? 0 : i - index;
  }

  [[nodiscard]] constexpr token scan_number(cursor &c) const noexcept {
    const size_t start = c.pos;
    size_t i = start;
    token_kind kind = token_kind::integer;
    bool malformed = false;

    const char second = at(start + 1);
    const auto is_binary = [](char ch) { return ch == '0' || ch == '1'; };
    const auto is_octal = [](char ch) { return ch >= '0' && ch <= '7'; };
    if (rules_.hex_binary_prefixes && at(start) == '0' && (second == 'x' || second == 'X')) {
      i += 2;
      const size_t digits = scan_digits(i, is_hex_digit);
      i += digits;
      bool fraction = false;
      if (rules_.hex_float && at(i) == '.' && (is_hex_digit(at(i + 1)) || digits != 0)) {
        fraction = true;
        kind = token_kind::floating;
        i += 1 + scan_digits(i + 1, is_hex_digit);
      }
      const char p = at(i);
      if (rules_.hex_float && (p == 'p' || p == 'P')) {
        const char sign = at(i + 1);
        const size_t digit_at = (sign == '+' || sign == '-') ? i + 2 : i + 1;
        if (is_digit(at(digit_at))) {
          kind = token_kind::floating;
          i = digit_at + scan_digits(digit_at, is_digit);
        }
      }
      malformed = digits == 0 && !(fraction && i > start + 3);
    } else if (rules_.hex_binary_prefixes && at(start) == '0' && (second == 'b' || second == 'B')) {
      i += 2;
      const size_t digits = scan_digits(i, is_binary);
      malformed = digits == 0;
      i += digits;
    } else if (rules_.octal_prefix && at(start) == '0' && (second == 'o' || second == 'O')) {
      i += 2;
      const size_t digits = scan_digits(i, is_octal);
      malformed = digits == 0;
      i += digits;
    } else {
      i += scan_digits(i, is_digit);
      if (at(i) == '.' && is_digit(at(i + 1))) {
        kind = token_kind::floating;
        i += 1 + scan_digits(i + 1, is_digit);
      } else if (rules_.decimal_trailing_dot && at(i) == '.' && i > start && at(i + 1) != '.') {
        kind = token_kind::floating;
        ++i;
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

    if (!malformed) {
      i += number_suffix_length(i);
    }
    // A number glued to identifier characters (`12ab`) is malformed as a whole, unless the language allows
    // user-defined suffixes (the parser validates them).
    if (i < source_.size() && is_base_ident_continue(source_[i])) {
      malformed = malformed || !rules_.user_defined_suffixes;
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

      if (rules_.directive_prefix != '\0' && ch == rules_.directive_prefix && first_on_line(c.pos)) {
        return emit(c, token_kind::directive, scan_directive(c.pos));
      }

      if (const size_t marker = line_comment_marker(c.pos); marker != 0) {
        size_t level = 0;
        if (const size_t opener = rules_.long_bracket_comments ? long_bracket_open(c.pos + marker, level) : 0;
            opener != 0) {
          bool terminated = false;
          const size_t length = marker + scan_long_bracket(c.pos + marker, opener, level, terminated);
          if (!terminated) {
            return emit(c, token_kind::error, length);
          }
          if (rules_.skip_comments) {
            advance_to(c, c.pos + length);
            continue;
          }
          return emit(c, token_kind::comment, length);
        }
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

      if (rules_.long_bracket_strings) {
        size_t level = 0;
        if (const size_t opener = long_bracket_open(c.pos, level); opener != 0) {
          bool terminated = false;
          const size_t length = scan_long_bracket(c.pos, opener, level, terminated);
          return emit(c, terminated ? token_kind::string : token_kind::error, length);
        }
      }

      if (contains_byte(rules_.quotes, ch) || contains_byte(rules_.char_quotes, ch)) {
        return scan_quote_like(c, 0);
      }
      if (!rules_.string_prefixes.empty() && is_ident_start(ch)) {
        if (const size_t prefix = string_prefix_length(c.pos); prefix != 0) {
          return scan_quote_like(c, prefix);
        }
      }

      if (rules_.numbers && rules_.datetime_literals && is_digit(ch)) {
        if (const size_t length = datetime_length(c.pos); length != 0) {
          return emit(c, token_kind::datetime, length);
        }
      }

      if (rules_.numbers && (is_digit(ch) || (ch == '.' && is_digit(at(c.pos + 1))))) {
        return scan_number(c);
      }

      if (is_ident_start(ch)) {
        size_t i = c.pos + 1;
        while (i < source_.size() && is_ident_continue(source_[i])) {
          ++i;
        }
        const size_t length = i - c.pos;
        const microfmt::string_view word = source_.substr(c.pos, length);
        const bool is_keyword = rules_.keywords_ignore_case ? rules_.keywords.contains_ignore_case(word)
                                                            : rules_.keywords.contains(word);
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
