// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "exception_frame.hpp"
#include "frame_pointer.hpp"

namespace microfmt {

// ============================================================================
// Unified Unwind Frame Type
// ============================================================================

enum class frame_kind : uint8_t { standard = 0, trap_transition };

struct hybrid_frame {
  uint32_t frame_index{0};
  uintptr_t fp{0};
  uintptr_t pc{0};
  frame_kind kind{frame_kind::standard};
  trap_context trap{}; // Valid when kind == frame_kind::trap_transition
};

// ============================================================================
// Exception Matcher Hook
// ============================================================================

// Type-erased hook to detect whether the current (FP, PC) sits at an exception
// trampoline
class exception_matcher_ref {
public:
  struct vtable {
    bool (*match_trap_frame)(const void *ctx, uintptr_t fp, uintptr_t pc,
                             uintptr_t &out_trap_frame_addr) noexcept;
  };

  constexpr exception_matcher_ref() noexcept = default;

  template <typename Tag, typename Context>
  constexpr exception_matcher_ref(Tag, const Context &ctx) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag, Context>) {}

  template <typename Fn>
  constexpr explicit exception_matcher_ref(const Fn &fn) noexcept
      : ctx_(&fn), vtbl_(&s_fn_vtbl<Fn>) {}

  [[nodiscard]] bool
  match_trap_frame(uintptr_t fp, uintptr_t pc,
                   uintptr_t &out_trap_frame_addr) const noexcept {
    if (!vtbl_)
      return false;
    return vtbl_->match_trap_frame(ctx_, fp, pc, out_trap_frame_addr);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return vtbl_ != nullptr;
  }

private:
  template <typename Tag, typename Context>
  static constexpr vtable s_vtbl{[](const void *c, uintptr_t fp, uintptr_t pc,
                                    uintptr_t &out_addr) noexcept {
    return static_cast<const Context *>(c)->match_trap_frame(fp, pc, out_addr);
  }};

  template <typename Fn>
  static constexpr vtable s_fn_vtbl{[](const void *c, uintptr_t fp,
                                       uintptr_t pc,
                                       uintptr_t &out_addr) noexcept {
    return (*static_cast<const Fn *>(c))(fp, pc, out_addr);
  }};

  const void *ctx_{nullptr};
  const vtable *vtbl_{nullptr};
};

// ============================================================================
// Hybrid Stack Unwinder (FP + Exception Frames)
// ============================================================================

class hybrid_stack_unwinder {
public:
  constexpr hybrid_stack_unwinder(frame_unwinder_ref fp_unwinder,
                                  exception_frame_ref trap_decoder,
                                  exception_matcher_ref matcher,
                                  uintptr_t initial_fp,
                                  uintptr_t initial_pc) noexcept
      : fp_unwinder_(fp_unwinder), trap_decoder_(trap_decoder),
        matcher_(matcher),
        current_{0, initial_fp, initial_pc, frame_kind::standard, {}},
        is_valid_(initial_fp != 0 || initial_pc != 0) {}

  [[nodiscard]] constexpr const hybrid_frame &operator*() const noexcept {
    return current_;
  }
  [[nodiscard]] constexpr const hybrid_frame *operator->() const noexcept {
    return &current_;
  }
  [[nodiscard]] constexpr bool has_value() const noexcept { return is_valid_; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return is_valid_;
  }

  bool next() noexcept {
    if (!is_valid_)
      return false;

    // Check if the current frame is an exception trampoline
    uintptr_t trap_frame_addr = 0;
    if (matcher_ &&
        matcher_.match_trap_frame(current_.fp, current_.pc, trap_frame_addr)) {
      trap_context trap_info{};
      if (trap_decoder_ && trap_decoder_.decode(trap_frame_addr, trap_info)) {
        // Synthesize a trap transition frame
        current_.frame_index++;
        current_.kind = frame_kind::trap_transition;
        current_.trap = trap_info;
        current_.fp = trap_info.fp;
        current_.pc = trap_info.pc;
        return true;
      }
    }

    // Normal FP unwind step
    uintptr_t next_fp = 0;
    uintptr_t next_pc = 0;
    if (!fp_unwinder_.step(current_.fp, next_fp, next_pc)) {
      is_valid_ = false;
      return false;
    }

    current_.frame_index++;
    current_.kind = frame_kind::standard;
    current_.fp = next_fp;
    current_.pc = next_pc;
    return true;
  }

  hybrid_stack_unwinder &operator++() noexcept {
    next();
    return *this;
  }

  template <typename Visitor>
  void for_each_frame(Visitor &&visitor, uint32_t max_depth = 64) noexcept {
    while (is_valid_ && current_.frame_index < max_depth) {
      if (!visitor(current_)) {
        break;
      }
      next();
    }
  }

private:
  frame_unwinder_ref fp_unwinder_{};
  exception_frame_ref trap_decoder_{};
  exception_matcher_ref matcher_{};
  hybrid_frame current_{};
  bool is_valid_{false};
};

// ============================================================================
// Formattable Hybrid Backtrace View
// ============================================================================

class hybrid_backtrace_view {
public:
  constexpr hybrid_backtrace_view(hybrid_stack_unwinder unwinder,
                                  symbol_resolver_ref resolver,
                                  span<char> scratch,
                                  uint32_t max_depth = 32) noexcept
      : unwinder_(unwinder), resolver_(resolver), scratch_(scratch),
        max_depth_(max_depth) {}

  template <size_t N>
  constexpr hybrid_backtrace_view(hybrid_stack_unwinder unwinder,
                                  symbol_resolver_ref resolver,
                                  char (&scratch)[N],
                                  uint32_t max_depth = 32) noexcept
      : unwinder_(unwinder), resolver_(resolver), scratch_(scratch, N),
        max_depth_(max_depth) {}

  [[nodiscard]] constexpr hybrid_stack_unwinder unwinder() const noexcept {
    return unwinder_;
  }
  [[nodiscard]] constexpr symbol_resolver_ref resolver() const noexcept {
    return resolver_;
  }
  [[nodiscard]] constexpr span<char> scratch() const noexcept {
    return scratch_;
  }
  [[nodiscard]] constexpr uint32_t max_depth() const noexcept {
    return max_depth_;
  }

private:
  hybrid_stack_unwinder unwinder_;
  symbol_resolver_ref resolver_{};
  span<char> scratch_{};
  uint32_t max_depth_{32};
};

template <> struct formatter<hybrid_backtrace_view> {
  char mode{'\0'};

  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    if (!spec.empty() && spec.front() == '#') {
      mode = '#';
    }
  }

  void format(const hybrid_backtrace_view &view,
              const sink &out) const noexcept {
    hybrid_stack_unwinder cursor = view.unwinder();

    bool first = true;
    cursor.for_each_frame(
        [&](const hybrid_frame &frame) noexcept -> bool {
          if (!first) {
            out.write("\n");
          }
          first = false;

          if (frame.kind == frame_kind::trap_transition) {
            out.write("  --- [Exception Boundary: ");
            if (frame.trap.is_user_mode) {
              out.write("User -> Kernel");
            } else {
              out.write("Nested Fault");
            }
            microfmt::format_to(out, " (vec {:#x})] ---\n",
                                frame.trap.vector_or_reason);
          }

          microfmt::format_to(out, "  #{:<2} fp={:#x}  pc=", frame.frame_index,
                              frame.fp);

          remote_fn_ptr fn_sym(frame.pc, view.resolver(), view.scratch());
          if (mode == '#') {
            microfmt::format_to(out, "{:#}", fn_sym);
          } else {
            microfmt::format_to(out, "{}", fn_sym);
          }
          return true;
        },
        view.max_depth());
  }
};

} // namespace microfmt