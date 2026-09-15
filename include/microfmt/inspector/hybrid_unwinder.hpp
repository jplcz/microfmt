// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

/** @file hybrid_unwinder.hpp @brief Hybrid stack unwinder combining standard
 * frame pointers with exception/trap transition frames. */

#include "exception_frame.hpp"
#include "frame_pointer.hpp"

namespace microfmt {

// ============================================================================
// Unified Unwind Frame Type
// ============================================================================

/**
 * @brief Classifies a `hybrid_frame` as either a normal stack frame or a trap
 * transition.
 */
enum class frame_kind : uint8_t { standard = 0, trap_transition };

/**
 * @brief One frame record produced during hybrid unwinding.
 */
struct hybrid_frame {
  /**
   * @brief Zero-based frame index (depth).
   */
  uint32_t frame_index{0};
  /**
   * @brief Frame/saved-stack pointer of the frame.
   */
  uintptr_t fp{0};
  /**
   * @brief Program counter (return address) of the frame.
   */
  uintptr_t pc{0};
  /**
   * @brief Frame classification.
   */
  frame_kind kind{frame_kind::standard};
  /**
   * @brief Trap context, valid when @ref kind is
   * @ref frame_kind::trap_transition.
   */
  trap_context trap{}; // Valid when kind == frame_kind::trap_transition
};

// ============================================================================
// Exception Matcher Hook
// ============================================================================

/**
 * @brief Type-erased hook detecting exception trampolines during unwinding.
 */
class exception_matcher_ref {
public:
  /**
   * @brief Virtual table of matcher operations.
   */
  struct vtable {
    /**
     * @brief Detects whether (fp, pc) sits at an exception trampoline. See
     * @ref exception_matcher_ref::match_trap_frame.
     */
    bool (*match_trap_frame)(const void *ctx, uintptr_t fp, uintptr_t pc,
                             uintptr_t &out_trap_frame_addr) noexcept;
  };

  /**
   * @brief Constructs an empty (invalid) matcher.
   */
  constexpr exception_matcher_ref() noexcept = default;

  /**
   * @brief Constructs a matcher bound to a context object exposing
   * `match_trap_frame`.
   * @tparam Tag Tag stored in the virtual table.
   * @tparam Context Concrete context type.
   * @param ctx Context object performing the match.
   */
  template <typename Tag, typename Context>
  constexpr exception_matcher_ref(Tag, const Context &ctx) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag, Context>) {}

  /**
   * @brief Constructs a matcher bound to a free callable.
   * @tparam Fn Callable type invoked as `(fp, pc, out_addr)`.
   * @param fn Object to invoke for matching.
   */
  template <typename Fn>
  constexpr explicit exception_matcher_ref(const Fn &fn) noexcept
      : ctx_(&fn), vtbl_(&s_fn_vtbl<Fn>) {}

  /**
   * @brief Tests whether (fp, pc) is an exception trampoline.
   * @param fp Current frame pointer.
   * @param pc Current program counter.
   * @param out_trap_frame_addr Receives the trap frame address.
   * @return `true` when matched.
   */
  [[nodiscard]] bool
  match_trap_frame(uintptr_t fp, uintptr_t pc,
                   uintptr_t &out_trap_frame_addr) const noexcept {
    if (!vtbl_)
      return false;
    return vtbl_->match_trap_frame(ctx_, fp, pc, out_trap_frame_addr);
  }

  /**
   * @brief Reports whether the matcher is bound.
   * @return `true` when the matcher is valid.
   */
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

/**
 * @brief Forward-only cursor combining frame-pointer stepping with
 * exception/trap transitions.
 */
class hybrid_stack_unwinder {
public:
  /**
   * @brief Constructs a hybrid unwinder.
   * @param fp_unwinder Frame-pointer unwinder for standard frames.
   * @param trap_decoder Trap-frame decoder for transition frames.
   * @param matcher Exception trampoline detector.
   * @param initial_fp Frame pointer of the starting frame.
   * @param initial_pc Program counter of the starting frame.
   */
  constexpr hybrid_stack_unwinder(frame_unwinder_ref fp_unwinder,
                                  exception_frame_ref trap_decoder,
                                  exception_matcher_ref matcher,
                                  uintptr_t initial_fp,
                                  uintptr_t initial_pc) noexcept
      : fp_unwinder_(fp_unwinder), trap_decoder_(trap_decoder),
        matcher_(matcher),
        current_{0, initial_fp, initial_pc, frame_kind::standard, {}},
        is_valid_(initial_fp != 0 || initial_pc != 0) {}

  /**
   * @brief Returns the current frame record.
   * @return Reference to the current @ref hybrid_frame.
   */
  [[nodiscard]] constexpr const hybrid_frame &operator*() const noexcept {
    return current_;
  }
  /**
   * @brief Returns the current frame record.
   * @return Pointer to the current @ref hybrid_frame.
   */
  [[nodiscard]] constexpr const hybrid_frame *operator->() const noexcept {
    return &current_;
  }

  /**
   * @brief Reports whether the cursor is positioned on a frame.
   * @return `true` while the current frame is valid.
   */
  [[nodiscard]] constexpr bool has_value() const noexcept { return is_valid_; }
  /**
   * @brief Reports whether the cursor is positioned on a frame.
   * @return `true` while the current frame is valid.
   */
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return is_valid_;
  }

  /**
   * @brief Advances to the next frame.
   *
   * Synthesizes a trap-transition frame when the current (fp, pc) matches an
   * exception trampoline; otherwise performs a standard frame-pointer step.
   *
   * @return `false` when the walk can advance no further.
   */
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

  /**
   * @brief Pre-increment advancing to the next frame.
   * @return This unwinder.
   */
  hybrid_stack_unwinder &operator++() noexcept {
    next();
    return *this;
  }

  /**
   * @brief Visitor-style traversal over the hybrid frame chain.
   * @tparam Visitor Callable accepting `const hybrid_frame &` and returning
   * `bool` (`true` = continue).
   * @param visitor Visitor invoked per frame.
   * @param max_depth Maximum number of frames to visit.
   */
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
  /// Frame-pointer unwinder.
  frame_unwinder_ref fp_unwinder_{};
  /// Trap-frame decoder.
  exception_frame_ref trap_decoder_{};
  /// Exception trampoline matcher.
  exception_matcher_ref matcher_{};
  /// Current frame record.
  hybrid_frame current_{};
  /// Validity flag.
  bool is_valid_{false};
};

// ============================================================================
// Formattable Hybrid Backtrace View
// ============================================================================

/**
 * @brief Formattable view rendering a hybrid backtrace.
 */
class hybrid_backtrace_view {
public:
  /**
   * @brief Constructs a hybrid backtrace view.
   * @param unwinder Unwinder cursor driving the walk.
   * @param resolver Symbol resolver for frame PCs.
   * @param scratch Scratch buffer for symbol strings.
   * @param max_depth Maximum number of frames to render.
   */
  constexpr explicit hybrid_backtrace_view(hybrid_stack_unwinder &unwinder,
                                           symbol_resolver_ref resolver,
                                           span<char> scratch,
                                           uint32_t max_depth = 32) noexcept
      : unwinder_(unwinder), resolver_(resolver), scratch_(scratch),
        max_depth_(max_depth) {}

  /**
   * @brief Constructs a hybrid backtrace view with a C-array scratch buffer.
   * @tparam N Scratch buffer size.
   * @param unwinder Unwinder cursor driving the walk.
   * @param resolver Symbol resolver for frame PCs.
   * @param scratch Scratch buffer for symbol strings.
   * @param max_depth Maximum number of frames to render.
   */
  template <size_t N>
  constexpr hybrid_backtrace_view(hybrid_stack_unwinder &unwinder,
                                  symbol_resolver_ref resolver,
                                  char (&scratch)[N],
                                  uint32_t max_depth = 32) noexcept
      : unwinder_(unwinder), resolver_(resolver), scratch_(scratch, N),
        max_depth_(max_depth) {}

  /**
   * @brief Returns the underlying unwinder.
   * @return Reference to the @ref hybrid_stack_unwinder.
   */
  [[nodiscard]] constexpr hybrid_stack_unwinder &unwinder() const noexcept {
    return unwinder_;
  }
  /**
   * @brief Returns the symbol resolver handle.
   * @return Bound @ref symbol_resolver_ref.
   */
  [[nodiscard]] constexpr symbol_resolver_ref resolver() const noexcept {
    return resolver_;
  }
  /**
   * @brief Returns the scratch buffer.
   * @return Scratch span used for symbol strings.
   */
  [[nodiscard]] constexpr span<char> scratch() const noexcept {
    return scratch_;
  }
  /**
   * @brief Returns the maximum frame depth.
   * @return Frame render limit.
   */
  [[nodiscard]] constexpr uint32_t max_depth() const noexcept {
    return max_depth_;
  }

private:
  /// Unwinder cursor, stored by reference to eliminate stack bloat.
  hybrid_stack_unwinder &unwinder_;
  /// Symbol resolver handle.
  symbol_resolver_ref resolver_{};
  /// Scratch span for symbol strings.
  span<char> scratch_{};
  /// Maximum number of frames to render.
  uint32_t max_depth_{32};
};

/**
 * @brief Formatter for @ref hybrid_backtrace_view.
 *
 * Emits one `#index fp=0x.. pc=<symbol>` line per frame, preceded by an
 * `[Exception Boundary]` marker at trap transitions. A leading `#` in the
 * specifier requests verbose symbol output.
 */
template <> struct formatter<hybrid_backtrace_view> {
  /**
   * @brief Output mode: `'#'` = verbose symbols, `'\0'` = standard.
   */
  char mode{'\0'};

  /**
   * @brief Parses the leading `#` mode flag.
   * @param ctx Format parse context exposing the specifier text.
   */
  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    if (!spec.empty() && spec.front() == '#') {
      mode = '#';
    }
  }

  /**
   * @brief Renders the hybrid backtrace.
   * @param view The backtrace view to format.
   * @param out Destination sink.
   */
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