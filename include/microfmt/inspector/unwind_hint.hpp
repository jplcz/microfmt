// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file unwind_hint.hpp @brief PC-range unwind hint tables for direct,
 * layout-based fallback unwinding. */

#include "register_context.hpp"
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace microfmt {

template <typename Tag> struct unwind_hint_registry_traits;
template <size_t MaxHints> class unwind_hint_registry_context;
template <size_t MaxHints = 32> struct fixed_unwind_hint_registry_tag {};

// ============================================================================
// Unwind Hint Definition
// ============================================================================

/**
 * @brief PC-range descriptor for direct, layout-based fallback unwinding.
 *
 * Unwind hints enable custom recovery routines for code regions that cannot
 * be unwound via standard EXIDX, DWARF, or frame-pointer strategies. Common
 * use cases include kernel context-switch stubs, scheduler trampolines,
 * interrupt return paths, and custom prologue sequences that temporarily
 * repurpose registers or saved context layouts.
 */
struct unwind_hint {
  /// Inclusive start of the covered PC range.
  uintptr_t pc_start{0};
  /// Exclusive end of the covered PC range.
  uintptr_t pc_end{0};

  /**
   * @brief Signature of a custom unwind routine provided by the hint.
   *
   * The routine receives a full register context handle (not just FP/PC) to
   * enable reading and writing arbitrary registers during context recovery.
   * This is essential for non-standard unwinds that:
   * - Store or recover return addresses in general-purpose registers (GPRs)
   * - Modify saved context as part of frame recovery
   * - Need platform-specific or scheduler-aware state interpretation
   *
   * @param space Target address space for memory reads/writes.
   * @param reg_ctx Register context handle supporting read/write access to
   *                 architecture-specific registers by DWARF index.
   *                 Use reg_ctx.read<T>(dwarf_reg_index, value) or
   *                 reg_ctx.write<T>(dwarf_reg_index, value) to access
   *                 general-purpose and special registers.
   * @param next_fp Output: recovered caller frame pointer.
   * @param next_pc Output: recovered caller program counter.
   * @return `true` when the routine successfully recovered the next frame.
   *         Return `false` if the context is invalid, saved registers are
   *         absent, or the recovered addresses cannot advance the walk.
   */
  using unwind_routine_t = bool (*)(address_space_ref space,
                                    register_context_ref reg_ctx,
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
class MICROFMT_POINTER unwind_hint_registry_ref {
public:
  /**
   * @brief Virtual table of registry operations.
   */
  struct vtable {
    /**
     * @brief Looks up a hint for a PC. See
     * @ref unwind_hint_registry_ref::find_hint.
     */
    bool (*find_hint)(const void *ctx, uintptr_t pc, unwind_hint &out_hint) noexcept;
  };

  /**
   * @brief Constructs an empty (invalid) handle.
   */
  constexpr unwind_hint_registry_ref() noexcept = default;

  /**
   * @brief Constructs a handle for a stateful registry tag.
   * @tparam Tag Registry tag type.
   * @tparam Context Concrete context convertible to the trait context type.
   * @param ctx Context object performing the lookup.
   */
  template <typename Tag, typename Context,
            typename Traits = unwind_hint_registry_traits<Tag>,
            std::enable_if_t<std::is_convertible_v<
                                 const Context *,
                                 const typename Traits::context_type *>,
                             int> = 0>
  constexpr unwind_hint_registry_ref(
      Tag, const Context &ctx MICROFMT_LIFETIMEBOUND
               MICROFMT_LIFETIME_CAPTURE_BY_THIS) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag>) {}

  template <typename Tag, typename Context,
            std::enable_if_t<!std::is_lvalue_reference_v<Context>, int> = 0>
  constexpr unwind_hint_registry_ref(Tag, Context &&) = delete;

  template <typename Tag, typename Context,
            typename Traits = unwind_hint_registry_traits<Tag>>
  [[nodiscard]] static constexpr unwind_hint_registry_ref
  make(const Context &ctx MICROFMT_LIFETIMEBOUND) noexcept {
    return unwind_hint_registry_ref(Tag{}, ctx);
  }

  template <typename Tag, typename Context,
            std::enable_if_t<!std::is_lvalue_reference_v<Context>, int> = 0>
  static unwind_hint_registry_ref make(Context &&) = delete;

  /**
   * @brief Looks up the hint covering a PC.
   * @param pc PC being looked up.
   * @param out_hint Receives the matching hint.
   * @return `true` when a hint matched.
   */
  [[nodiscard]] bool find_hint(uintptr_t pc, unwind_hint &out_hint) const noexcept {
    if (!vtbl_)
      return false;
    return vtbl_->find_hint(ctx_.get(), pc, out_hint);
  }

  /**
   * @brief Reports whether the handle is bound to a registry.
   * @return `true` when the handle is valid.
   */
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return vtbl_ != nullptr; }

private:
  template <typename Tag>
  static constexpr vtable s_vtbl{
      [](const void *context, uintptr_t pc, unwind_hint &hint) noexcept {
        using context_type =
            typename unwind_hint_registry_traits<Tag>::context_type;
        const auto &typed_context =
            *static_cast<const context_type *>(context);
        return unwind_hint_registry_traits<Tag>::find_hint(
            value_ref<const context_type>(typed_context), pc, hint);
      }};

  value_ptr<const void> ctx_{};
  const vtable *vtbl_{nullptr};
};

/**
 * @brief Typed owner for an unwind-hint registry traits specialization.
 */
template <typename Tag> class MICROFMT_OWNER unwind_hint_registry {
public:
  using traits_type = unwind_hint_registry_traits<Tag>;
  using context_type = typename traits_type::context_type;

  constexpr explicit unwind_hint_registry(context_type context) noexcept
      : context_(std::move(context)) {}

  [[nodiscard]] constexpr value_ref<context_type>
  context() & noexcept MICROFMT_LIFETIMEBOUND {
    return value_ref<context_type>(context_);
  }

  [[nodiscard]] constexpr value_ref<const context_type>
  context() const & noexcept MICROFMT_LIFETIMEBOUND {
    return value_ref<const context_type>(context_);
  }

  [[nodiscard]] constexpr unwind_hint_registry_ref
  ref() const & noexcept MICROFMT_LIFETIMEBOUND {
    return unwind_hint_registry_ref(Tag{}, context_);
  }

  [[nodiscard]] constexpr operator unwind_hint_registry_ref()
      const & noexcept MICROFMT_LIFETIMEBOUND {
    return ref();
  }

  value_ref<context_type> context() && = delete;
  value_ref<const context_type> context() const && = delete;
  unwind_hint_registry_ref ref() const && = delete;
  operator unwind_hint_registry_ref() const && = delete;

private:
  context_type context_;
};

// ============================================================================
// Concrete Zero-Allocation Fixed-Capacity Registry Context
// ============================================================================

/**
 * @brief Zero-allocation hint registry with fixed capacity.
 * @tparam MaxHints Maximum number of stored hints.
 */
template <size_t MaxHints>
class MICROFMT_OWNER unwind_hint_registry_context {
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
    MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

    hints_[hint_count_++] = hint;

    MICROFMT_END_UNSAFE_BUFFER_USAGE;

    return true;
  }

  /**
   * @brief Linear scan for the hint covering a PC.
   * @param pc PC being looked up.
   * @param out_hint Receives the matching hint.
   * @return `true` when a hint matched.
   */
  [[nodiscard]] constexpr bool find_hint(uintptr_t pc, unwind_hint &out_hint) const noexcept {
    MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

    for (size_t i = 0; i < hint_count_; ++i) {
      if (hints_[i].contains(pc)) {
        out_hint = hints_[i];
        return true;
      }
    }

    MICROFMT_END_UNSAFE_BUFFER_USAGE;

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
template <size_t MaxHints>
struct unwind_hint_registry_traits<fixed_unwind_hint_registry_tag<MaxHints>> {
  using context_type = unwind_hint_registry_context<MaxHints>;

  static bool find_hint(value_ref<const context_type> context, uintptr_t pc,
                        unwind_hint &out_hint) noexcept {
    return context->find_hint(pc, out_hint);
  }
};

using unwind_hint_registry_tag = fixed_unwind_hint_registry_tag<32>;

} // namespace microfmt
