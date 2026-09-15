// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file unwind_hint.hpp @brief PC-range unwind hint tables for direct,
 * layout-based fallback unwinding. */

#include <cstddef>
#include <cstdint>

namespace microfmt {

// ============================================================================
// Unwind Hint Definition
// ============================================================================

/**
 * @brief PC-range descriptor for direct, layout-based fallback unwinding.
 */
struct unwind_hint {
  /// Inclusive start of the covered PC range.
  uintptr_t pc_start{0};
  /// Exclusive end of the covered PC range.
  uintptr_t pc_end{0};

  /**
   * @brief Signature of a custom unwind routine provided by the hint.
   */
  using unwind_routine_t = bool (*)(address_space_ref space,
                                    uintptr_t current_fp, uintptr_t current_pc,
                                    uintptr_t &next_fp,
                                    uintptr_t &next_pc) noexcept;

  /**
   * @brief Custom routine handler, or `nullptr` to use layout-based fallback.
   */
  unwind_routine_t routine{nullptr};

  /**
   * @brief Reports whether a PC falls within the hint range.
   * @param pc PC to test.
   * @return `true` when `pc_start <= pc < pc_end`.
   */
  [[nodiscard]] constexpr bool contains(uintptr_t pc) const noexcept {
    return pc >= pc_start && pc < pc_end;
  }
};

// ============================================================================
// Type-Erased Unwind Hint Registry Handle
// ============================================================================

/**
 * @brief Type-erased, two-word handle to an unwind hint registry.
 */
class unwind_hint_registry_ref {
public:
  /**
   * @brief Virtual table of registry operations.
   */
  struct vtable {
    /**
     * @brief Looks up a hint for a PC. See
     * @ref unwind_hint_registry_ref::find_hint.
     */
    bool (*find_hint)(const void *ctx, uintptr_t pc,
                      unwind_hint &out_hint) noexcept;
  };

  /**
   * @brief Constructs an empty (invalid) handle.
   */
  constexpr unwind_hint_registry_ref() noexcept = default;

  /**
   * @brief Constructs a handle bound to a context object exposing
   * `find_hint`.
   * @tparam Tag Tag stored in the virtual table.
   * @tparam Context Concrete context type.
   * @param ctx Context object performing the lookup.
   */
  template <typename Tag, typename Context>
  constexpr unwind_hint_registry_ref(Tag, const Context &ctx) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag, Context>) {}

  /**
   * @brief Constructs a handle bound to a free callable.
   * @tparam Fn Callable type exposing `find_hint`.
   * @param fn Object to invoke for lookups.
   */
  template <typename Fn>
  constexpr explicit unwind_hint_registry_ref(const Fn &fn) noexcept
      : ctx_(&fn), vtbl_(&s_fn_vtbl<Fn>) {}

  /**
   * @brief Looks up the hint covering a PC.
   * @param pc PC being looked up.
   * @param out_hint Receives the matching hint.
   * @return `true` when a hint matched.
   */
  [[nodiscard]] bool find_hint(uintptr_t pc,
                               unwind_hint &out_hint) const noexcept {
    if (!vtbl_)
      return false;
    return vtbl_->find_hint(ctx_, pc, out_hint);
  }

  /**
   * @brief Reports whether the handle is bound to a registry.
   * @return `true` when the handle is valid.
   */
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return vtbl_ != nullptr;
  }

private:
  template <typename Tag, typename Context>
  static constexpr vtable s_vtbl{
      [](const void *c, uintptr_t pc, unwind_hint &hint) noexcept {
        return static_cast<const Context *>(c)->find_hint(pc, hint);
      }};

  template <typename Fn>
  static constexpr vtable s_fn_vtbl{
      [](const void *c, uintptr_t pc, unwind_hint &hint) noexcept {
        return (*static_cast<const Fn *>(c)).find_hint(pc, hint);
      }};

  const void *ctx_{nullptr};
  const vtable *vtbl_{nullptr};
};

// ============================================================================
// Concrete Zero-Allocation Fixed-Capacity Registry Context
// ============================================================================

/**
 * @brief Zero-allocation hint registry with fixed capacity.
 * @tparam MaxHints Maximum number of stored hints.
 */
template <size_t MaxHints = 32> class unwind_hint_registry_context {
public:
  /**
   * @brief Constructs an empty registry.
   */
  constexpr unwind_hint_registry_context() noexcept = default;

  /**
   * @brief Appends a hint to the registry.
   * @param hint Hint to store.
   * @return `false` when the registry is full.
   */
  constexpr bool add_hint(const unwind_hint &hint) noexcept {
    if (hint_count_ >= MaxHints)
      return false;
    hints_[hint_count_++] = hint;
    return true;
  }

  /**
   * @brief Linear scan for the hint covering a PC.
   * @param pc PC being looked up.
   * @param out_hint Receives the matching hint.
   * @return `true` when a hint matched.
   */
  [[nodiscard]] constexpr bool find_hint(uintptr_t pc,
                                         unwind_hint &out_hint) const noexcept {
    for (size_t i = 0; i < hint_count_; ++i) {
      if (hints_[i].contains(pc)) {
        out_hint = hints_[i];
        return true;
      }
    }
    return false;
  }

  /**
   * @brief Returns the number of stored hints.
   * @return Current hint count.
   */
  [[nodiscard]] constexpr size_t size() const noexcept { return hint_count_; }

private:
  /// Stored hints.
  unwind_hint hints_[MaxHints]{};
  /// Number of stored hints.
  size_t hint_count_{0};
};

/**
 * @brief Tag selecting the hint registry in the type-erased handle.
 */
struct unwind_hint_registry_tag {};

} // namespace microfmt