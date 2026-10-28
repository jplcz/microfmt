// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file operator_table.hpp
 * @brief Operators declared at run time: precedence levels, spellings and an `expression_grammar` view.
 *
 * Why: `parse_expression` normally takes `constexpr` operator arrays, which is right for a fixed language. A language
 * that declares operators itself (`infixl 6 +++`), a grammar read from a file, or a REPL that lets the user add an
 * operator cannot build arrays at compile time. `operator_table` owns the declarations and their spelling text,
 * checks them, lets you slot a new precedence level between existing ones, and keeps the lists the lexer needs in
 * step. See `docs/token-stream.md` ("Operators chosen at run time").
 */

#include "lexer.hpp"
#include "token_stream.hpp"
#include <cstddef>
#include <cstdint>
#include <reloco/expected.hpp>
#include <reloco/span.hpp>
#include <reloco/sso_vector.hpp>

namespace microfmt {

namespace detail {

/** Views a vector's elements; unlike `data()`, this is valid while the vector is empty. */
template <class T, class Vector> [[nodiscard]] reloco::span<const T> view_of(const Vector &v RELOCO_LIFETIMEBOUND) noexcept {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  return reloco::span<const T>(v.unsafe_data(), v.size());
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

/** Views a character vector as text; valid while the vector is empty. */
template <class Vector>
[[nodiscard]] microfmt::string_view string_view_of(const Vector &chars RELOCO_LIFETIMEBOUND) noexcept {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  return microfmt::string_view(chars.unsafe_data(), chars.size());
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

} // namespace detail

/** A precedence level of one `operator_table`. Levels are ordered; the numbers behind them are not public. */
struct RELOCO_COPYABLE level {
  static constexpr uint16_t kInvalid = 0xFFFFu;

  uint16_t id{kInvalid};

  [[nodiscard]] constexpr bool valid() const noexcept { return id != kInvalid; }
  friend constexpr bool operator==(level a, level b) noexcept { return a.id == b.id; }
  friend constexpr bool operator!=(level a, level b) noexcept { return a.id != b.id; }
};

/**
 * @brief Fixed-capacity owner of operator declarations, with levels instead of raw precedence numbers.
 *
 * `N` declarations live inside the table; more spill to the heap up to `max_operators`, which bounds hostile
 * input. Every declaration returns a static error message instead of failing silently. The views handed out by
 * `grammar()`, `spellings()` and `keywords()` are rebuilt by each successful declaration and stay valid until
 * the next one: declare operators between `parse_expression` calls, never from inside one.
 */
template <size_t N> class RELOCO_OWNER operator_table {
  static_assert(N > 0, "operator_table needs room for at least one operator");

public:
  /** Success, or a static message such as "duplicate operator". */
  using status = reloco::expected<void, microfmt::string_view>;
  template <typename T> using result = reloco::expected<T, microfmt::string_view>;

  /** Levels are renumbered into `uint8_t` precedences: `kBase + rank * kStep`; 62 levels fit. */
  static constexpr size_t kMaxLevels = 62;

  explicit operator_table(size_t max_operators = 256) noexcept : max_operators_(max_operators) {
    const microfmt::string_view defaults = "(){}[],;";
    (void)punctuation_.try_append(reloco::span<const char>(defaults.data(), defaults.size()));
  }

  operator_table(const operator_table &) = delete;
  operator_table &operator=(const operator_table &) = delete;

  // ---- levels ------------------------------------------------------------------------------------------------

  /** A new level that binds tighter than every existing one (the first call makes the loosest). */
  [[nodiscard]] result<level> add_level() noexcept { return insert_level(order_.size(), false, 0); }

  /** A new level that binds immediately tighter than `reference`. */
  [[nodiscard]] result<level> add_level_above(level reference) noexcept {
    const size_t at = position_of(reference);
    if (at == kNone) {
      return reloco::unexpected<microfmt::string_view>("bad precedence");
    }
    return insert_level(at + 1, false, 0);
  }

  /** A new level that binds immediately looser than `reference`. */
  [[nodiscard]] result<level> add_level_below(level reference) noexcept {
    const size_t at = position_of(reference);
    if (at == kNone) {
      return reloco::unexpected<microfmt::string_view>("bad precedence");
    }
    return insert_level(at, false, 0);
  }

  /** The `uint8_t` precedence a level currently has in `grammar()` (changes when levels are inserted). */
  [[nodiscard]] size_t precedence_of(level l) const noexcept {
    const size_t at = position_of(l);
    return at == kNone ? 0 : kBase + at * kStep;
  }

  /** The level of a declared operator (binary first, then prefix, postfix, mixfix); invalid when unknown. */
  [[nodiscard]] level level_of(microfmt::string_view text) const noexcept {
    for (size_t category = 0; category < 4; ++category) {
      for (size_t i = 0; i < entries_.size(); ++i) {
        if (static_cast<size_t>(entries_[i].category) == category && spelling_of(entries_[i]) == text) {
          return level{entries_[i].level_id};
        }
      }
    }
    return level{};
  }

  // ---- declarations ------------------------------------------------------------------------------------------

  [[nodiscard]] status add_binary(microfmt::string_view text, level at, assoc associativity = assoc::left) noexcept {
    return declare(category::binary, text, at, associativity);
  }
  /** Numeric form for tables that really are fixed: creates or reuses the level numbered `precedence`. */
  [[nodiscard]] status add_binary(microfmt::string_view text, uint8_t precedence,
                                  assoc associativity = assoc::left) noexcept {
    return declare_numeric(category::binary, text, precedence, associativity);
  }

  [[nodiscard]] status add_prefix(microfmt::string_view text, level at) noexcept {
    return declare(category::prefix, text, at, assoc::left);
  }
  [[nodiscard]] status add_prefix(microfmt::string_view text, uint8_t precedence) noexcept {
    return declare_numeric(category::prefix, text, precedence, assoc::left);
  }

  [[nodiscard]] status add_postfix(microfmt::string_view text, level at) noexcept {
    return declare(category::postfix, text, at, assoc::left);
  }
  [[nodiscard]] status add_postfix(microfmt::string_view text, uint8_t precedence) noexcept {
    return declare_numeric(category::postfix, text, precedence, assoc::left);
  }

  [[nodiscard]] status add_mixfix(microfmt::string_view text, level at) noexcept {
    return declare(category::mixfix, text, at, assoc::left);
  }
  [[nodiscard]] status add_mixfix(microfmt::string_view text, uint8_t precedence) noexcept {
    return declare_numeric(category::mixfix, text, precedence, assoc::left);
  }

  /** Enables `cond ? then : else`; the spellings are copied. Its precedence is `op.precedence`, as given. */
  [[nodiscard]] status set_ternary(const ternary_op &op) noexcept {
    if (!valid_spelling(op.question) || !valid_spelling(op.colon)) {
      return reloco::unexpected<microfmt::string_view>("empty operator");
    }
    const size_t mark = arena_.size();
    const reloco::optional<span_ref> question = store(op.question);
    const reloco::optional<span_ref> colon = question ? store(op.colon) : reloco::optional<span_ref>{};
    if (!question || !colon) {
      arena_.truncate(mark);
      return reloco::unexpected<microfmt::string_view>("operator table full");
    }
    const ternary_slot saved = ternary_;
    ternary_ = ternary_slot{true, *question, *colon, op.precedence};
    if (!rebuild()) {
      ternary_ = saved;
      arena_.truncate(mark);
      (void)rebuild(false);
      return reloco::unexpected<microfmt::string_view>("operator table full");
    }
    return {};
  }

  /**
   * The bytes the lexer treats as punctuation. A single-byte operator spelling in this set (a call `(` or an
   * index `[`) is left out of `spellings()`, so it stays punctuation. Default: `(){}[],;`.
   */
  [[nodiscard]] status set_punctuation(microfmt::string_view bytes) noexcept {
    reloco::sso_vector<char, 16> saved;
    if (!saved.try_append(detail::view_of<char>(punctuation_))) {
      return reloco::unexpected<microfmt::string_view>("operator table full");
    }
    punctuation_.clear();
    if (!punctuation_.try_append(reloco::span<const char>(bytes.data(), bytes.size())) || !rebuild()) {
      punctuation_.clear();
      (void)punctuation_.try_append(detail::view_of<char>(saved));
      (void)rebuild(false);
      return reloco::unexpected<microfmt::string_view>("operator table full");
    }
    return {};
  }

  // ---- views -------------------------------------------------------------------------------------------------

  /** The tables for `parse_expression(ts, grammar, actions)`. */
  [[nodiscard]] const expression_grammar &grammar() const & noexcept RELOCO_LIFETIMEBOUND { return grammar_; }
  const expression_grammar &grammar() const && = delete;

  /** Symbol-like spellings (`+`, `<=`, `+++`) for `lexer_rules::operators`. */
  [[nodiscard]] span<const microfmt::string_view> spellings() const & noexcept RELOCO_LIFETIMEBOUND {
    return detail::view_of<microfmt::string_view>(spellings_);
  }
  span<const microfmt::string_view> spellings() const && = delete;

  /** Word-like spellings (`div`, `and`, `if`) for `lexer_rules::keywords`, sorted as the lexer requires. */
  [[nodiscard]] sorted_words keywords() const & noexcept RELOCO_LIFETIMEBOUND {
    return sorted_words::from_sorted(detail::view_of<microfmt::string_view>(keywords_));
  }
  sorted_words keywords() const && = delete;

  /** Number of declared operators (an operator declared in two categories counts twice). */
  [[nodiscard]] size_t size() const noexcept { return entries_.size(); }

  /** Counts every successful change; compare before and after to detect that views were rebuilt. */
  [[nodiscard]] uint32_t generation() const noexcept { return generation_; }

  /** Removes every operator and level (the punctuation set is kept). */
  void clear() noexcept {
    entries_.clear();
    arena_.clear();
    levels_.clear();
    order_.clear();
    ternary_ = ternary_slot{};
    (void)rebuild();
  }

private:
  static constexpr size_t kNone = static_cast<size_t>(-1);
  static constexpr size_t kBase = 8;
  static constexpr size_t kStep = 4;

  enum class category : uint8_t { binary, prefix, postfix, mixfix };

  struct RELOCO_COPYABLE span_ref {
    uint32_t offset{0};
    uint32_t length{0};
  };
  struct RELOCO_COPYABLE entry {
    span_ref text{};
    category category{category::binary};
    uint16_t level_id{0};
    assoc associativity{assoc::left};
  };
  struct RELOCO_COPYABLE level_info {
    bool numeric{false};
    uint8_t number{0};
  };
  struct RELOCO_COPYABLE ternary_slot {
    bool enabled{false};
    span_ref question{};
    span_ref colon{};
    uint8_t precedence{5};
  };

  [[nodiscard]] static bool valid_spelling(microfmt::string_view text) noexcept {
    if (text.empty()) {
      return false;
    }
    for (size_t i = 0; i < text.size(); ++i) {
      const auto ch = static_cast<unsigned char>(text[i]);
      if (ch <= 0x20u || ch == 0x7Fu) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] static bool is_word(microfmt::string_view text) noexcept {
    for (size_t i = 0; i < text.size(); ++i) {
      const char ch = text[i];
      const bool letter = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
      const bool digit = ch >= '0' && ch <= '9';
      if (!letter && !(digit && i > 0)) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] microfmt::string_view spelling_of(const entry &e) const noexcept { return text_at(e.text); }

  [[nodiscard]] microfmt::string_view text_at(span_ref r) const noexcept {
    return detail::string_view_of(arena_).substr(r.offset, r.length);
  }

  [[nodiscard]] reloco::optional<span_ref> store(microfmt::string_view text) noexcept {
    const size_t start = arena_.size();
    if (!arena_.try_append(reloco::span<const char>(text.data(), text.size()))) {
      return reloco::optional<span_ref>{};
    }
    return reloco::optional<span_ref>{span_ref{static_cast<uint32_t>(start & 0xFFFFFFFFu),
                                               static_cast<uint32_t>(text.size() & 0xFFFFFFFFu)}};
  }

  [[nodiscard]] size_t position_of(level l) const noexcept {
    for (size_t i = 0; i < order_.size(); ++i) {
      if (order_[i] == l.id) {
        return i;
      }
    }
    return kNone;
  }

  [[nodiscard]] result<level> insert_level(size_t at, bool numeric, uint8_t number) noexcept {
    if (order_.size() >= kMaxLevels || levels_.size() >= kMaxLevels) {
      return reloco::unexpected<microfmt::string_view>("operator table full");
    }
    const auto id = static_cast<uint16_t>(levels_.size() & 0xFFFFu);
    if (!levels_.try_push_back(level_info{numeric, number})) {
      return reloco::unexpected<microfmt::string_view>("operator table full");
    }
    if (!order_.try_insert_at(at, id)) {
      levels_.truncate(levels_.size() - 1);
      return reloco::unexpected<microfmt::string_view>("operator table full");
    }
    if (!rebuild()) {
      (void)order_.try_erase_at(at);
      levels_.truncate(levels_.size() - 1);
      (void)rebuild(false);
      return reloco::unexpected<microfmt::string_view>("operator table full");
    }
    return level{id};
  }

  // Finds (or creates) the level a numeric precedence stands for, ordered among the other numeric levels.
  [[nodiscard]] result<level> level_for_number(uint8_t number) noexcept {
    size_t after_smaller = kNone;
    size_t before_greater = kNone;
    for (size_t i = 0; i < order_.size(); ++i) {
      const level_info &info = levels_[order_[i]];
      if (!info.numeric) {
        continue;
      }
      if (info.number == number) {
        return level{order_[i]};
      }
      if (info.number < number) {
        after_smaller = i;
      } else if (before_greater == kNone) {
        before_greater = i;
      }
    }
    size_t at = order_.size();
    if (after_smaller != kNone) {
      at = after_smaller + 1;
    } else if (before_greater != kNone) {
      at = before_greater;
    }
    return insert_level(at, true, number);
  }

  // Rejects what can be rejected without touching the table, so a failed declaration creates no level.
  [[nodiscard]] status precheck(category kind, microfmt::string_view text) const noexcept {
    if (!valid_spelling(text)) {
      return reloco::unexpected<microfmt::string_view>("empty operator");
    }
    for (size_t i = 0; i < entries_.size(); ++i) {
      if (entries_[i].category == kind && spelling_of(entries_[i]) == text) {
        return reloco::unexpected<microfmt::string_view>("duplicate operator");
      }
    }
    return {};
  }

  [[nodiscard]] status declare_numeric(category kind, microfmt::string_view text, uint8_t number,
                                       assoc associativity) noexcept {
    if (auto checked = precheck(kind, text); !checked) {
      return checked;
    }
    auto at = level_for_number(number);
    return at ? declare(kind, text, at.value(), associativity)
              : status(reloco::unexpected<microfmt::string_view>(at.error()));
  }

  [[nodiscard]] status declare(category kind, microfmt::string_view text, level at, assoc associativity) noexcept {
    if (auto checked = precheck(kind, text); !checked) {
      return checked;
    }
    if (position_of(at) == kNone) {
      return reloco::unexpected<microfmt::string_view>("bad precedence");
    }
    if (entries_.size() >= max_operators_) {
      return reloco::unexpected<microfmt::string_view>("operator table full");
    }
    const size_t mark = arena_.size();
    const reloco::optional<span_ref> where = store(text);
    if (!where || !entries_.try_push_back(entry{*where, kind, at.id, associativity})) {
      arena_.truncate(mark);
      return reloco::unexpected<microfmt::string_view>("operator table full");
    }
    if (!rebuild()) {
      entries_.truncate(entries_.size() - 1);
      arena_.truncate(mark);
      (void)rebuild(false);
      return reloco::unexpected<microfmt::string_view>("operator table full");
    }
    return {};
  }

  [[nodiscard]] bool contains_byte(microfmt::string_view set, char ch) const noexcept {
    return set.find(ch) != microfmt::string_view::npos;
  }

  [[nodiscard]] bool has_word(microfmt::string_view text, const reloco::sso_vector<microfmt::string_view, N> &list) const
      noexcept {
    for (size_t i = 0; i < list.size(); ++i) {
      if (list[i] == text) {
        return true;
      }
    }
    return false;
  }

  // Recomputes every view from the declarations. False when memory ran out (the views are then unusable until a
  // later rebuild succeeds; callers roll the declaration back and rebuild again).
  [[nodiscard]] bool rebuild(bool count_generation = true) noexcept {
    binary_.clear();
    prefix_.clear();
    postfix_.clear();
    mixfix_.clear();
    spellings_.clear();
    keywords_.clear();
    bool ok = true;
    const microfmt::string_view punctuation = detail::string_view_of(punctuation_);
    for (size_t i = 0; i < entries_.size() && ok; ++i) {
      const entry &e = entries_[i];
      const microfmt::string_view text = spelling_of(e);
      const size_t rank = position_of(level{e.level_id});
      const auto precedence = static_cast<uint8_t>((kBase + rank * kStep) & 0xFFu);
      switch (e.category) {
      case category::binary:
        ok = binary_.try_push_back(binary_op{text, precedence, e.associativity}).has_value();
        break;
      case category::prefix:
        ok = prefix_.try_push_back(prefix_op{text, precedence}).has_value();
        break;
      case category::postfix:
        ok = postfix_.try_push_back(postfix_op{text, precedence}).has_value();
        break;
      case category::mixfix:
        ok = mixfix_.try_push_back(mixfix_op{text, precedence}).has_value();
        break;
      }
      if (!ok) {
        break;
      }
      if (is_word(text)) {
        if (!has_word(text, keywords_)) {
          size_t at = 0;
          while (at < keywords_.size() && keywords_[at] < text) {
            ++at;
          }
          ok = keywords_.try_insert_at(at, text).has_value();
        }
      } else if (!(text.size() == 1 && contains_byte(punctuation, text[0])) && !has_word(text, spellings_)) {
        ok = spellings_.try_push_back(text).has_value();
      }
    }
    grammar_.binary = detail::view_of<binary_op>(binary_);
    grammar_.prefix = detail::view_of<prefix_op>(prefix_);
    grammar_.postfix = detail::view_of<postfix_op>(postfix_);
    grammar_.mixfix = detail::view_of<mixfix_op>(mixfix_);
    if (ternary_.enabled) {
      grammar_.ternary = ternary_op{text_at(ternary_.question), text_at(ternary_.colon), ternary_.precedence};
    } else {
      grammar_.ternary = reloco::optional<ternary_op>{};
    }
    if (ok && count_generation) {
      ++generation_;
    }
    return ok;
  }

  size_t max_operators_;
  reloco::sso_vector<entry, N> entries_;
  reloco::sso_vector<char, 256> arena_;
  reloco::sso_vector<level_info, 16> levels_;
  reloco::sso_vector<uint16_t, 16> order_; // level ids, loosest first
  reloco::sso_vector<char, 16> punctuation_;
  ternary_slot ternary_{};

  reloco::sso_vector<binary_op, N> binary_;
  reloco::sso_vector<prefix_op, N> prefix_;
  reloco::sso_vector<postfix_op, N> postfix_;
  reloco::sso_vector<mixfix_op, N> mixfix_;
  reloco::sso_vector<microfmt::string_view, N> spellings_;
  reloco::sso_vector<microfmt::string_view, N> keywords_;
  expression_grammar grammar_{};
  uint32_t generation_{0};
};

} // namespace microfmt
