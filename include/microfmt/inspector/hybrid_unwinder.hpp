// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file hybrid_unwinder.hpp @brief Hybrid stack unwinder combining standard
 * frame pointers with exception/trap transition frames. */

#include "exception_frame.hpp"
#include "frame_pointer.hpp"
#include <type_traits>
#include <utility>

namespace microfmt {

template <typename Tag> struct exception_matcher_traits;

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
class MICROFMT_POINTER exception_matcher_ref {
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
   * @brief Constructs a matcher bound to a traits context.
   * @tparam Tag Matcher implementation tag.
   * @tparam Context Concrete context convertible to the trait context type.
   * @param ctx Context object performing the match.
   */
  template <typename Tag, typename Context,
            typename Traits = exception_matcher_traits<Tag>,
            std::enable_if_t<std::is_convertible_v<
                                 const Context *,
                                 const typename Traits::context_type *>,
                             int> = 0>
  constexpr exception_matcher_ref(
      Tag, const Context &ctx MICROFMT_LIFETIMEBOUND
               MICROFMT_LIFETIME_CAPTURE_BY_THIS) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag>) {}

  template <typename Tag, typename Context,
            std::enable_if_t<!std::is_lvalue_reference_v<Context>, int> = 0>
  constexpr exception_matcher_ref(Tag, Context &&) = delete;

  template <typename Tag, typename Context,
            typename Traits = exception_matcher_traits<Tag>>
  [[nodiscard]] static constexpr exception_matcher_ref
  make(const Context &ctx MICROFMT_LIFETIMEBOUND) noexcept {
    return exception_matcher_ref(Tag{}, ctx);
  }

  template <typename Tag, typename Context,
            std::enable_if_t<!std::is_lvalue_reference_v<Context>, int> = 0>
  static exception_matcher_ref make(Context &&) = delete;

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
    return vtbl_->match_trap_frame(ctx_.get(), fp, pc,
                                   out_trap_frame_addr);
  }

  /**
   * @brief Reports whether the matcher is bound.
   * @return `true` when the matcher is valid.
   */
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return vtbl_ != nullptr;
  }

private:
  template <typename Tag>
  static constexpr vtable s_vtbl{
      [](const void *context, uintptr_t fp, uintptr_t pc,
         uintptr_t &out_addr) noexcept {
        using context_type = typename exception_matcher_traits<Tag>::context_type;
        const auto &typed_context =
            *static_cast<const context_type *>(context);
        return exception_matcher_traits<Tag>::match_trap_frame(
            value_ref<const context_type>(typed_context), fp, pc, out_addr);
      }};

  value_ptr<const void> ctx_{};
  const vtable *vtbl_{nullptr};
};

/**
 * @brief Typed owner for an exception-matcher traits specialization.
 */
template <typename Tag> class MICROFMT_OWNER exception_matcher {
public:
  using traits_type = exception_matcher_traits<Tag>;
  using context_type = typename traits_type::context_type;

  constexpr explicit exception_matcher(context_type context) noexcept
      : context_(std::move(context)) {}

  [[nodiscard]] constexpr value_ref<context_type>
  context() & noexcept MICROFMT_LIFETIMEBOUND {
    return value_ref<context_type>(context_);
  }

  [[nodiscard]] constexpr value_ref<const context_type>
  context() const & noexcept MICROFMT_LIFETIMEBOUND {
    return value_ref<const context_type>(context_);
  }

  [[nodiscard]] constexpr exception_matcher_ref
  ref() const & noexcept MICROFMT_LIFETIMEBOUND {
    return exception_matcher_ref(Tag{}, context_);
  }

  [[nodiscard]] constexpr operator exception_matcher_ref()
      const & noexcept MICROFMT_LIFETIMEBOUND {
    return ref();
  }

  value_ref<context_type> context() && = delete;
  value_ref<const context_type> context() const && = delete;
  exception_matcher_ref ref() const && = delete;
  operator exception_matcher_ref() const && = delete;

private:
  context_type context_;
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
   * @param reg_ctx Target register context.
   * @param initial_fp Frame pointer of the starting frame.
   * @param initial_pc Program counter of the starting frame.
   */
  constexpr hybrid_stack_unwinder(frame_unwinder_ref fp_unwinder,
                                  exception_frame_ref trap_decoder,
                                  exception_matcher_ref matcher,
                                  register_context_ref reg_ctx,
                                  uintptr_t initial_fp,
                                  uintptr_t initial_pc) noexcept
      : fp_unwinder_(fp_unwinder), trap_decoder_(trap_decoder),
        matcher_(matcher), reg_ctx_(reg_ctx),
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
      current_.trap = {};
      if (trap_decoder_ &&
          trap_decoder_.decode(trap_frame_addr, current_.trap)) {
        // Synthesize a trap transition frame
        current_.frame_index++;
        current_.kind = frame_kind::trap_transition;
        current_.fp = current_.trap.fp;
        current_.pc = current_.trap.pc;
        return true;
      }
    }

    // Normal FP unwind step
    uintptr_t next_fp = 0;
    uintptr_t next_pc = 0;
    if (!fp_unwinder_.step(reg_ctx_, current_.pc, next_fp, next_pc)) {
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
  /// Target register context.
  register_context_ref reg_ctx_{};
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
class MICROFMT_POINTER hybrid_backtrace_view {
public:
  /**
   * @brief Constructs a hybrid backtrace view.
   * @param unwinder Unwinder cursor driving the walk.
   * @param resolver Symbol resolver for frame PCs.
   * @param symbol_context Caller-owned symbol-resolution scratch and state.
   * @param max_depth Maximum number of frames to render.
   */
  constexpr explicit hybrid_backtrace_view(
                                           hybrid_stack_unwinder &unwinder
                                               MICROFMT_LIFETIMEBOUND,
                                           symbol_resolver_ref resolver,
                                           symbol_resolution_context
                                               &symbol_context
                                                   MICROFMT_LIFETIMEBOUND,
                                           uint32_t max_depth = 32) noexcept
      : unwinder_(&unwinder), resolver_(resolver),
        symbol_context_(&symbol_context), max_depth_(max_depth) {}

  /**
   * @brief Returns the underlying unwinder.
   * @return Reference to the @ref hybrid_stack_unwinder.
   */
  [[nodiscard]] constexpr hybrid_stack_unwinder &
  unwinder() const noexcept MICROFMT_LIFETIMEBOUND {
    return *unwinder_;
  }
  /**
   * @brief Returns the symbol resolver handle.
   * @return Bound @ref symbol_resolver_ref.
   */
  [[nodiscard]] constexpr symbol_resolver_ref resolver() const noexcept {
    return resolver_;
  }
  [[nodiscard]] constexpr symbol_resolution_context &
  symbol_context() const noexcept MICROFMT_LIFETIMEBOUND {
    return *symbol_context_;
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
  value_ptr<hybrid_stack_unwinder> unwinder_;
  /// Symbol resolver handle.
  symbol_resolver_ref resolver_{};
  /// Caller-owned symbol-resolution scratch and state.
  value_ptr<symbol_resolution_context> symbol_context_{};
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
            microfmt::format_to(out, MICROFMT_STRING(" (vec {:#x})] ---\n"),
                                frame.trap.vector_or_reason);
          }

          microfmt::format_to(out, MICROFMT_STRING("  #{:<2} fp={:#x}  pc="),
                              frame.frame_index, frame.fp);

          remote_fn_ptr fn_sym(frame.pc, view.resolver(),
                               view.symbol_context());
          if (mode == '#') {
            microfmt::format_to(out, MICROFMT_STRING("{:#}"), fn_sym);
          } else {
            microfmt::format_to(out, MICROFMT_STRING("{}"), fn_sym);
          }
          return true;
        },
        view.max_depth());
  }
};

} // namespace microfmt