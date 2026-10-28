// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file ast_pool.hpp
 * @brief A pool of syntax-tree nodes addressed by small typed handles, with rollback for backtracking.
 *
 * Why: parsers build trees, and a tree needs somewhere to live. Raw pointers are unsafe, heap-allocated
 * children are slow and recurse on destruction, and a node cannot contain itself by value. A pool stores
 * every node in one `sso_vector` and links them by index (`node_id`), so a tree is a plain, copyable
 * value, small trees need no heap, and "undo a failed alternative" is truncating the vector.
 * See `docs/token-stream.md` ("Building a tree") for the guide.
 */

#include "token_stream.hpp"
#include <cstddef>
#include <cstdint>
#include <reloco/sso_vector.hpp>
#include <reloco/value_ref.hpp>
#include <type_traits>
#include <utility>

namespace microfmt {

/** Typed index of a node inside a `node_pool<T, N>`. Four bytes, trivially copyable; `invalid()` means "no node". */
template <typename T> struct RELOCO_COPYABLE node_id {
  static constexpr uint32_t kInvalid = 0xFFFFFFFFu;

  uint32_t index{kInvalid};

  [[nodiscard]] static constexpr node_id invalid() noexcept { return node_id{}; }
  [[nodiscard]] constexpr bool valid() const noexcept { return index != kInvalid; }

  friend constexpr bool operator==(node_id a, node_id b) noexcept { return a.index == b.index; }
  friend constexpr bool operator!=(node_id a, node_id b) noexcept { return a.index != b.index; }
};

/** The pool size at some moment; `node_pool::rollback` returns to it. */
struct RELOCO_COPYABLE pool_mark {
  size_t size{0};
};

/**
 * @brief Append-only pool of `T` nodes; undo by truncation.
 *
 * `N` nodes live inside the pool object; more spill to the heap (`reloco::sso_vector`) up to `max_nodes`, which
 * bounds hostile input. Handles are indices, so growth never invalidates them. A handle made after a `mark()`
 * is stale after `rollback(mark)`: `try_get` then returns an empty pointer (or a different node if the index
 * was reused), so do not keep such handles.
 */
template <typename T, size_t N> class RELOCO_OWNER node_pool {
public:
  static constexpr size_t kDefaultMaxNodes = 0xFFFFFFFEu;

  explicit node_pool(size_t max_nodes = kDefaultMaxNodes) noexcept
      : max_nodes_(max_nodes < kDefaultMaxNodes ? max_nodes : kDefaultMaxNodes) {}

  /** Stores `value`; fails with `node pool full` located at `at` when the limit or the allocator is exhausted. */
  [[nodiscard]] RELOCO_CONSTEXPR20 reloco::expected<node_id<T>, parse_error> make(const token &at, T value) noexcept {
    if (nodes_.size() >= max_nodes_ || !nodes_.try_push_back(std::move(value))) {
      return reloco::unexpected<parse_error>(parse_error{"node pool full", at});
    }
    return node_id<T>{static_cast<uint32_t>(nodes_.size() - 1)};
  }

  /** The node, or an empty pointer for a handle that is invalid, out of range or rolled back. */
  [[nodiscard]] reloco::value_ptr<T> try_get(node_id<T> id) & noexcept RELOCO_LIFETIMEBOUND {
    if (!id.valid() || id.index >= nodes_.size()) {
      return nullptr;
    }
    return reloco::value_ptr<T>(&nodes_[id.index]);
  }
  [[nodiscard]] reloco::value_ptr<const T> try_get(node_id<T> id) const & noexcept RELOCO_LIFETIMEBOUND {
    if (!id.valid() || id.index >= nodes_.size()) {
      return nullptr;
    }
    return reloco::value_ptr<const T>(&nodes_[id.index]);
  }

  /** The node; terminates on a bad handle (a programming error, like an out-of-range index). */
  [[nodiscard]] reloco::value_ref<T> get(node_id<T> id) & noexcept RELOCO_LIFETIMEBOUND {
    return reloco::value_ref<T>(nodes_[id.index]);
  }
  [[nodiscard]] reloco::value_ref<const T> get(node_id<T> id) const & noexcept RELOCO_LIFETIMEBOUND {
    return reloco::value_ref<const T>(nodes_[id.index]);
  }

  [[nodiscard]] size_t size() const noexcept { return nodes_.size(); }
  [[nodiscard]] size_t max_nodes() const noexcept { return max_nodes_; }

  /** Remembers the current size. */
  [[nodiscard]] pool_mark mark() const noexcept { return pool_mark{nodes_.size()}; }

  /** Drops every node made after `m` (no-op when the pool is not larger than `m`). */
  void rollback(pool_mark m) noexcept {
    if (m.size < nodes_.size()) {
      nodes_.truncate(m.size);
    }
  }

  /** Drops all nodes. */
  void clear() noexcept { nodes_.clear(); }

private:
  reloco::sso_vector<T, N> nodes_;
  size_t max_nodes_;
};

/**
 * @brief Wraps a parse rule so a failure also rolls the pool back.
 *
 * The result is a callable `(token_stream &)` returning what `rule` returns, so it plugs into `attempt`,
 * `parse_choice` and `parse_optional`: a failed alternative leaves neither input consumed nor nodes allocated.
 * Do not combine with `memoize` on the same pool (a rolled-back index can be reused; see the guide).
 * The wrapper keeps a reference to `pool` and a copy of `rule`.
 */
template <typename T, size_t N, typename Rule>
[[nodiscard]] auto pooled(node_pool<T, N> &pool RELOCO_LIFETIMEBOUND, Rule &&rule) noexcept {
  return [&pool, wrapped = std::decay_t<Rule>(std::forward<Rule>(rule))](token_stream &ts) mutable noexcept {
    const pool_mark before = pool.mark();
    auto result = wrapped(ts);
    if (!result) {
      pool.rollback(before);
    }
    return result;
  };
}

} // namespace microfmt
