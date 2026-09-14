#pragma once

#include <cstddef>
#include <cstdint>

namespace microfmt {

// ============================================================================
// Unwind Hint Definition
// ============================================================================

struct unwind_hint {
  uintptr_t pc_start{0};
  uintptr_t pc_end{0};
  int32_t sp_offset{0}; // Fixed stack adjustment for stripped routines
  bool restores_fp{false};
  bool is_assembly_stub{false};

  [[nodiscard]] constexpr bool contains(uintptr_t pc) const noexcept {
    return pc >= pc_start && pc < pc_end;
  }
};

// ============================================================================
// Type-Erased Unwind Hint Registry Handle
// ============================================================================

class unwind_hint_registry_ref {
public:
  struct vtable {
    bool (*find_hint)(const void *ctx, uintptr_t pc,
                      unwind_hint &out_hint) noexcept;
  };

  constexpr unwind_hint_registry_ref() noexcept = default;

  template <typename Tag, typename Context>
  constexpr unwind_hint_registry_ref(Tag, const Context &ctx) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag, Context>) {}

  template <typename Fn>
  constexpr explicit unwind_hint_registry_ref(const Fn &fn) noexcept
      : ctx_(&fn), vtbl_(&s_fn_vtbl<Fn>) {}

  [[nodiscard]] bool find_hint(uintptr_t pc,
                               unwind_hint &out_hint) const noexcept {
    if (!vtbl_)
      return false;
    return vtbl_->find_hint(ctx_, pc, out_hint);
  }

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

template <size_t MaxHints = 32> class unwind_hint_registry_context {
public:
  constexpr unwind_hint_registry_context() noexcept = default;

  constexpr bool add_hint(const unwind_hint &hint) noexcept {
    if (hint_count_ >= MaxHints)
      return false;
    hints_[hint_count_++] = hint;
    return true;
  }

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

  [[nodiscard]] constexpr size_t size() const noexcept { return hint_count_; }

private:
  unwind_hint hints_[MaxHints]{};
  size_t hint_count_{0};
};

struct unwind_hint_registry_tag {};

} // namespace microfmt