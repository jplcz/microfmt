// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file string_pool.hpp
 * @brief An interner for identifiers and literals: each distinct string is stored once and named by a 4-byte id.
 *
 * Why: tokens carry `string_view`s into the source, which dangle once streaming input is discarded and cannot
 * describe text that was built (a decoded string literal). A tree node that stores a `string_id` instead is small,
 * trivially copyable, outlives the source, and two names are equal exactly when their ids are equal. Lookup hashes the
 * text (FNV-1a), uses the low bits as a bucket, and walks a short chain of entries linked by index (no per-string
 * allocation); undo of a failed alternative is truncation, like `node_pool`.
 * See `docs/token-stream.md` ("Strings in a tree") for the guide, including how the hashing works.
 */

#include "string_pool_ref.hpp"
#include "token_stream.hpp"
#include <cstddef>
#include <cstdint>
#include <reloco/function_ref.hpp>
#include <reloco/span.hpp>
#include <reloco/sso_vector.hpp>
#include <reloco/string_view.hpp>
#include <reloco/value_ref.hpp>
#include <type_traits>
#include <utility>

namespace microfmt {

/** The pool contents at some moment; `string_pool::rollback` returns to it. */
struct RELOCO_COPYABLE string_pool_mark {
  size_t strings{0};
  size_t bytes{0};
};

/**
 * @brief Case-sensitive interner: equal text always gives an equal `string_id`.
 *
 * `N` bytes of text live inside the pool object; more spill to the heap (`reloco::sso_vector`) up to `max_bytes`,
 * and at most `max_strings` distinct strings are kept, which bounds hostile input. Ids are indices, so growth
 * never invalidates them; views from `view()` do not survive growth or rollback, so keep the id, not the view.
 * An id made after a `mark()` is stale after `rollback(mark)` (empty view, or another string if the index was
 * reused).
 */
template <size_t N> class RELOCO_OWNER string_pool {
  struct entry {
    uint32_t offset;
    uint32_t length;
    uint32_t hash;
    uint32_t next;
  };

  static constexpr uint32_t kNone = 0xFFFFFFFFu;
  static constexpr size_t kInitialBuckets = 16;

public:
  static constexpr size_t kDefaultMax = 0xFFFFFFFEu;

  /**
   * @brief Writes one string piece by piece straight into the pool (for literals with escapes, joined names).
   *
   * Get one from `string_pool::begin()`. Only one builder can be open per pool; while it is open `intern` fails
   * with `string pool busy` and `rollback`/`clear` do nothing. A failed `append` (a limit was hit) is remembered
   * and reported by `finish`, so a decoder does not need to check every piece. Do not append a view obtained from
   * `view(id)` (the arena may move while growing).
   */
  class RELOCO_POINTER builder {
  public:
    builder(const builder &) = delete;
    builder &operator=(const builder &) = delete;
    ~builder() { abandon(); }

    /** Adds `piece` to the text. */
    void append(reloco::string_view piece) noexcept {
      if (!active_ || failed_) {
        return;
      }
      string_pool &p = *pool_;
      if (piece.size() > p.max_bytes_ - p.arena_.size() ||
          !p.arena_.try_append(reloco::span<const char>(piece.data(), piece.size()))) {
        failed_ = true;
      }
    }

    void push_back(char c) noexcept { append(reloco::string_view(&c, 1)); }

    /** The text so far; valid until the next `append`. */
    [[nodiscard]] reloco::string_view view() const & noexcept RELOCO_LIFETIMEBOUND {
      if (!active_) {
        return {};
      }
      return pool_->text_from(start_);
    }

    /** Stores the text (or finds an equal existing string and drops the copy) and closes the builder. */
    [[nodiscard]] reloco::expected<string_id, parse_error> finish(const token &at) noexcept {
      if (!active_) {
        return reloco::unexpected<parse_error>(parse_error{message_, at});
      }
      string_pool &p = *pool_;
      if (failed_) {
        abandon();
        return reloco::unexpected<parse_error>(parse_error{"string pool full", at});
      }
      const size_t length = p.arena_.size() - start_;
      const uint32_t hash = hash_of(p.text_from(start_));
      const uint32_t existing = p.find_index(p.text_from(start_), hash);
      if (existing != kNone) {
        abandon();
        return string_id{existing};
      }
      const bool added = p.commit(start_, length, hash);
      active_ = false;
      p.building_ = false;
      if (!added) {
        return reloco::unexpected<parse_error>(parse_error{"string pool full", at});
      }
      return string_id{static_cast<uint32_t>(p.entries_.size() - 1)};
    }

    /** Drops the text and closes the builder (also done by the destructor). */
    void abandon() noexcept {
      if (!active_) {
        return;
      }
      pool_->arena_.truncate(start_);
      pool_->building_ = false;
      active_ = false;
    }

  private:
    friend string_pool;

    explicit builder(string_pool &pool RELOCO_LIFETIMEBOUND) noexcept
        : pool_(pool), start_(pool.arena_.size()), active_(!pool.building_) {
      if (active_) {
        pool.building_ = true;
      } else {
        failed_ = true;
        message_ = "string pool busy";
      }
    }

    reloco::value_ref<string_pool> pool_;
    size_t start_;
    bool active_;
    bool failed_{false};
    reloco::string_view message_{"string pool full"};
  };

  explicit string_pool(size_t max_bytes = kDefaultMax, size_t max_strings = kDefaultMax) noexcept
      : max_bytes_(max_bytes < kDefaultMax ? max_bytes : kDefaultMax),
        max_strings_(max_strings < kDefaultMax ? max_strings : kDefaultMax) {}

  string_pool(const string_pool &) = delete;
  string_pool &operator=(const string_pool &) = delete;

  /** The id of `text`, adding the string if it is new. Fails with `string pool full` (or `busy`) located at `at`. */
  [[nodiscard]] reloco::expected<string_id, parse_error> intern(reloco::string_view text, const token &at) noexcept {
    if (building_) {
      return reloco::unexpected<parse_error>(parse_error{"string pool busy", at});
    }
    const uint32_t hash = hash_of(text);
    const uint32_t existing = find_index(text, hash);
    if (existing != kNone) {
      return string_id{existing};
    }
    if (entries_.size() >= max_strings_ || text.size() > max_bytes_ - arena_.size() || !ensure_buckets()) {
      return reloco::unexpected<parse_error>(parse_error{"string pool full", at});
    }
    const size_t start = arena_.size();
    if (!arena_.try_append(reloco::span<const char>(text.data(), text.size())) || !commit(start, text.size(), hash)) {
      return reloco::unexpected<parse_error>(parse_error{"string pool full", at});
    }
    return string_id{static_cast<uint32_t>(entries_.size() - 1)};
  }

  /** Interns the token's text (for a string literal the text includes the quotes; build the value with `begin()`). */
  [[nodiscard]] reloco::expected<string_id, parse_error> intern(const token &at) noexcept {
    return intern(at.text, at);
  }

  /**
   * Interns each word in order. In an empty pool the words get the ids `0..k-1`, so a keyword test is
   * `id.index < k`. Words must be distinct for that numbering to hold.
   */
  [[nodiscard]] reloco::expected<void, parse_error> intern_all(reloco::span<const reloco::string_view> words,
                                                               const token &at) noexcept {
    for (size_t i = 0; i < words.size(); ++i) {
      auto id = intern(words[i], at);
      if (!id) {
        return reloco::unexpected<parse_error>(id.error());
      }
    }
    return {};
  }

  /** The id of `text` if it is already in the pool, otherwise `string_id::invalid()`. Never inserts. */
  [[nodiscard]] string_id find(reloco::string_view text) const noexcept {
    const uint32_t found = find_index(text, hash_of(text));
    return found == kNone ? string_id::invalid() : string_id{found};
  }

  /** The text, or an empty view for an invalid, out-of-range or rolled-back id. */
  [[nodiscard]] reloco::string_view view(string_id id) const & noexcept RELOCO_LIFETIMEBOUND {
    if (!id.valid() || id.index >= entries_.size()) {
      return {};
    }
    const entry &e = entries_[id.index];
    return text_from(e.offset).substr(0, e.length);
  }
  reloco::string_view view(string_id) const && = delete;

  /** Starts building a string piece by piece (see `builder`). */
  [[nodiscard]] builder begin() & noexcept RELOCO_LIFETIMEBOUND { return builder(*this); }

  /**
   * The type-erased handle that `token_stream` and `expect_name`/`expect_string` use. Keep the pool alive and in
   * place for as long as the handle is used, and keep the handle itself alive as long as the stream that borrows it.
   */
  [[nodiscard]] string_pool_ref ref() & noexcept RELOCO_LIFETIMEBOUND {
    return string_pool_ref(string_pool_ref::intern_fn(reloco::nontype<&string_pool::intern_erased>, *this),
                           string_pool_ref::decode_fn(reloco::nontype<&string_pool::decode_erased>, *this),
                           string_pool_ref::view_fn(reloco::nontype<&string_pool::view_erased>, *this));
  }

  [[nodiscard]] size_t size() const noexcept { return entries_.size(); }
  [[nodiscard]] size_t bytes() const noexcept { return arena_.size(); }
  [[nodiscard]] size_t max_bytes() const noexcept { return max_bytes_; }
  [[nodiscard]] size_t max_strings() const noexcept { return max_strings_; }

  /** Remembers the current contents. */
  [[nodiscard]] string_pool_mark mark() const noexcept { return string_pool_mark{entries_.size(), arena_.size()}; }

  /** Drops every string added after `m`. No-op while a builder is open. */
  void rollback(string_pool_mark m) noexcept {
    if (building_) {
      return;
    }
    // Entries are removed newest first, so each one is the head of its bucket chain and unlinking is one store.
    while (entries_.size() > m.strings) {
      const entry &last = entries_[entries_.size() - 1];
      buckets_[last.hash & (buckets_.size() - 1)] = last.next;
      entries_.truncate(entries_.size() - 1);
    }
    if (m.bytes < arena_.size()) {
      arena_.truncate(m.bytes);
    }
  }

  /** Drops all strings. No-op while a builder is open. */
  void clear() noexcept {
    if (building_) {
      return;
    }
    entries_.clear();
    arena_.clear();
    for (size_t i = 0; i < buckets_.size(); ++i) {
      buckets_[i] = kNone;
    }
  }

private:
  [[nodiscard]] static pool_result intern_erased(string_pool &pool, reloco::string_view text) noexcept {
    const auto result = pool.intern(text, token{});
    return result ? pool_result{result.value(), {}} : pool_result{string_id{}, result.error().message};
  }

  [[nodiscard]] static pool_result decode_erased(string_pool &pool, reloco::string_view raw,
                                                 string_decoder decoder) noexcept {
    builder out = pool.begin();
    auto append = [&out](reloco::string_view piece) noexcept { out.append(piece); };
    string_sink sink{reloco::function_ref<void(reloco::string_view)>(append)};
    decoder(sink, raw);
    if (sink.failed()) {
      out.abandon();
      return pool_result{string_id{}, sink.failure()};
    }
    const auto result = out.finish(token{});
    return result ? pool_result{result.value(), {}} : pool_result{string_id{}, result.error().message};
  }

  [[nodiscard]] static reloco::string_view view_erased(const string_pool &pool RELOCO_LIFETIMEBOUND, string_id id) noexcept {
    return pool.view(id);
  }

  // FNV-1a: fast and simple. Not a security boundary; `max_strings` bounds the work hostile input can cause.
  [[nodiscard]] static uint32_t hash_of(reloco::string_view text) noexcept {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < text.size(); ++i) {
      h ^= static_cast<uint8_t>(text[i]);
      h *= 16777619u;
    }
    return h;
  }

  [[nodiscard]] reloco::string_view text_from(size_t offset) const & noexcept RELOCO_LIFETIMEBOUND {
    return reloco::string_view(arena_.data(), arena_.size()).substr(offset);
  }
  reloco::string_view text_from(size_t) const && = delete;

  [[nodiscard]] uint32_t find_index(reloco::string_view text, uint32_t hash) const noexcept {
    if (buckets_.empty()) {
      return kNone;
    }
    uint32_t i = buckets_[hash & (buckets_.size() - 1)];
    while (i != kNone) {
      const entry &e = entries_[i];
      if (e.hash == hash && e.length == text.size() && text_from(e.offset).substr(0, e.length) == text) {
        return i;
      }
      i = e.next;
    }
    return kNone;
  }

  [[nodiscard]] bool ensure_buckets() noexcept {
    if (!buckets_.empty()) {
      return true;
    }
    if (!buckets_.try_resize(kInitialBuckets)) {
      return false;
    }
    for (size_t i = 0; i < buckets_.size(); ++i) {
      buckets_[i] = kNone;
    }
    return true;
  }

  void link(uint32_t index) noexcept {
    entry &e = entries_[index];
    uint32_t &head = buckets_[e.hash & (buckets_.size() - 1)];
    e.next = head;
    head = index;
  }

  // Doubles the table when the load passes 1. Best effort: if memory is short the pool keeps working with
  // longer chains. Relinking in index order keeps the newest entry at the head of its chain (rollback relies on it).
  void maybe_grow() noexcept {
    if (entries_.size() <= buckets_.size() || !buckets_.try_resize(buckets_.size() * 2)) {
      return;
    }
    for (size_t i = 0; i < buckets_.size(); ++i) {
      buckets_[i] = kNone;
    }
    for (uint32_t i = 0; i < entries_.size(); ++i) {
      link(i);
    }
  }

  // Registers bytes already in the arena at [offset, offset + length). On failure the bytes are dropped.
  [[nodiscard]] bool commit(size_t offset, size_t length, uint32_t hash) noexcept {
    if (offset >= kNone || length >= kNone) {
      arena_.truncate(offset);
      return false;
    }
    if (entries_.size() >= max_strings_ || !ensure_buckets()) {
      arena_.truncate(offset);
      return false;
    }
    const entry added{static_cast<uint32_t>(offset & kNone), static_cast<uint32_t>(length & kNone), hash, kNone};
    if (!entries_.try_push_back(added)) {
      arena_.truncate(offset);
      return false;
    }
    link(static_cast<uint32_t>(entries_.size() - 1));
    maybe_grow();
    return true;
  }

  reloco::sso_vector<char, N> arena_;
  reloco::sso_vector<entry, 8> entries_;
  reloco::sso_vector<uint32_t, kInitialBuckets> buckets_;
  size_t max_bytes_;
  size_t max_strings_;
  bool building_{false};
};

/**
 * @brief Wraps a parse rule so a failure also rolls the string pool back.
 *
 * Same contract as `pooled` for `node_pool`: plug the result into `attempt`/`parse_choice`/`parse_optional`.
 * To roll several pools back together, nest the wrappers. Do not combine with `memoize` on ids of the same pool.
 */
template <size_t N, typename Rule>
[[nodiscard]] auto pooled(string_pool<N> &pool RELOCO_LIFETIMEBOUND, Rule &&rule) noexcept {
  return [&pool, wrapped = std::decay_t<Rule>(std::forward<Rule>(rule))](token_stream &ts) mutable noexcept {
    const string_pool_mark before = pool.mark();
    auto result = wrapped(ts);
    if (!result) {
      pool.rollback(before);
    }
    return result;
  };
}

} // namespace microfmt
