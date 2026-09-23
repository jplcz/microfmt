// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file frame_pointer.hpp @brief Type-erased frame unwinder handles, frame
 * cursor iteration, and backtrace views. */

#include "address_space.hpp"
#include "register_context.hpp"
#include "remote_diagnostics.hpp"
#include "symbol_resolver.hpp"
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace microfmt {

// ============================================================================
// Stack Frame Record
// ============================================================================

/**
 * @brief One frame record produced during stack unwinding.
 */
struct MICROFMT_API_CLASS stack_frame {
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
   * @brief Reports whether the record is an empty sentinel.
   * @return `true` when both @ref fp and @ref pc are zero.
   */
  [[nodiscard]] constexpr bool is_null() const noexcept {
    return fp == 0 && pc == 0;
  }
};

// ============================================================================
// Customization Traits Point
// ============================================================================

/**
 * @brief Static customization point describing a frame unwinder backend.
 * @tparam ArchTag Tag identifying the unwinder implementation.
 */
template <typename ArchTag> struct frame_unwinder_traits;

// ============================================================================
// Type-Erased Unwinder Handle (2 Words)
// ============================================================================

/**
 * @brief Type-erased, two-word handle to a frame unwinder.
 *
 * Packs a context pointer and a virtual table into two words, avoiding
 * allocations, RTTI, and virtual dispatch.
 */
class RELOCO_POINTER frame_unwinder_ref {
public:
  /**
   * @brief Virtual table of unwinder operations.
   */
  struct vtable {
    /**
     * @brief Advances one frame using register context.
     */
    bool (*step)(const void *ctx, register_context_ref reg_ctx,
                 uintptr_t current_pc, uintptr_t &next_fp,
                 uintptr_t &next_pc) noexcept;
  };

  /**
   * @brief Constructs an empty (invalid) handle.
   */
  constexpr frame_unwinder_ref() noexcept = default;

  /**
   * @brief Constructs a handle for a stateless unwinder tag.
   */
  template <
      typename ArchTag, typename Traits = frame_unwinder_traits<ArchTag>,
      std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  constexpr explicit frame_unwinder_ref(ArchTag) noexcept
      : ctx_(nullptr), vtbl_(&s_vtbl<ArchTag>) {}

  /**
   * @brief Constructs a handle for a stateful unwinder tag.
   */
  template <typename ArchTag, typename Context,
            typename Traits = frame_unwinder_traits<ArchTag>,
            std::enable_if_t<
                !std::is_void_v<typename Traits::context_type> &&
                    std::is_convertible_v<
                        const Context *, const typename Traits::context_type *>,
                int> = 0>
  constexpr frame_unwinder_ref(
      ArchTag, const Context &ctx RELOCO_LIFETIMEBOUND
                   RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<ArchTag>) {}

  template <typename ArchTag, typename Context,
            std::enable_if_t<!std::is_lvalue_reference_v<Context>, int> = 0>
  constexpr frame_unwinder_ref(ArchTag, Context &&) = delete;

  template <
      typename ArchTag, typename Traits = frame_unwinder_traits<ArchTag>,
      std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr frame_unwinder_ref make() noexcept {
    return frame_unwinder_ref(ArchTag{});
  }

  template <
      typename ArchTag, typename Context,
      typename Traits = frame_unwinder_traits<ArchTag>,
      std::enable_if_t<!std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr frame_unwinder_ref
  make(const Context &ctx RELOCO_LIFETIMEBOUND) noexcept {
    return frame_unwinder_ref(ArchTag{}, ctx);
  }

  template <typename ArchTag, typename Context,
            std::enable_if_t<!std::is_lvalue_reference_v<Context>, int> = 0>
  static frame_unwinder_ref make(Context &&) = delete;

  /**
   * @brief Advances from the current frame to its caller using
   * register_context_ref.
   *
   * @param reg_ctx Target register context handle.
   * @param current_pc Program counter of the frame being unwound *from*
   * (supplied by the caller/iterator, not read out of @p reg_ctx); backends
   * use this purely to look up the applicable unwind descriptor (`.ARM.exidx`
   * entry, DWARF FDE, unwind hint, ...) for the current frame. Register
   * restoration (SP/FP/LR/...) still happens via @p reg_ctx itself.
   * @param next_fp Receives the caller's frame pointer.
   * @param next_pc Receives the caller's program counter.
   */
  [[nodiscard]] bool step(register_context_ref reg_ctx, uintptr_t current_pc,
                          uintptr_t &next_fp,
                          uintptr_t &next_pc) const noexcept {
    if (!vtbl_)
      return false;
    return vtbl_->step(ctx_.get(), reg_ctx, current_pc, next_fp, next_pc);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return vtbl_ != nullptr;
  }

private:
  template <typename ArchTag>
  static bool step_entry(const void *context, register_context_ref registers,
                         uintptr_t current_pc, uintptr_t &next_fp,
                         uintptr_t &next_pc) noexcept {
    using context_type = typename frame_unwinder_traits<ArchTag>::context_type;
    if constexpr (std::is_void_v<context_type>) {
      return frame_unwinder_traits<ArchTag>::step(registers, current_pc,
                                                  next_fp, next_pc);
    } else {
      const auto &typed_context =
          *static_cast<const context_type *>(context);
      return frame_unwinder_traits<ArchTag>::step(
          value_ref<const context_type>(typed_context), registers, current_pc,
          next_fp, next_pc);
    }
  }

  template <typename ArchTag>
  static constexpr vtable s_vtbl{&step_entry<ArchTag>};

  value_ptr<const void> ctx_{};
  const vtable *vtbl_{nullptr};
};

template <typename Tag,
          bool Stateless =
              std::is_void_v<typename frame_unwinder_traits<Tag>::context_type>>
class frame_unwinder;

template <typename Tag> class RELOCO_OWNER frame_unwinder<Tag, false> {
public:
  using traits_type = frame_unwinder_traits<Tag>;
  using context_type = typename traits_type::context_type;

  constexpr explicit frame_unwinder(context_type context) noexcept
      : context_(std::move(context)) {}

  [[nodiscard]] constexpr value_ref<context_type>
  context() & noexcept RELOCO_LIFETIMEBOUND {
    return value_ref<context_type>(context_);
  }

  [[nodiscard]] constexpr value_ref<const context_type>
  context() const & noexcept RELOCO_LIFETIMEBOUND {
    return value_ref<const context_type>(context_);
  }

  [[nodiscard]] constexpr frame_unwinder_ref
  ref() const & noexcept RELOCO_LIFETIMEBOUND {
    return frame_unwinder_ref(Tag{}, context_);
  }

  value_ref<context_type> context() && = delete;
  value_ref<const context_type> context() const && = delete;
  frame_unwinder_ref ref() const && = delete;

private:
  context_type context_;
};

template <typename Tag> class frame_unwinder<Tag, true> {
public:
  [[nodiscard]] static constexpr frame_unwinder_ref ref() noexcept {
    return frame_unwinder_ref(Tag{});
  }
};

// ============================================================================
// Type-Erased Frame Pointer Iterator / Cursor
// ============================================================================

/**
 * @brief Forward-only cursor iterating over a stack walk backed by register
 * context.
 */
class MICROFMT_API_CLASS frame_pointer_iterator {
public:
  constexpr frame_pointer_iterator() noexcept = default;

  /**
   * @brief Constructs an iterator rooted at the given frame and register
   * context.
   *
   * @p initial_pc is tracked internally and fed back into the unwinder's
   * `step()` on every call, purely to let the backend look up the unwind
   * descriptor covering the *current* frame (`.ARM.exidx` entry, DWARF FDE,
   * unwind hint, ...). Callers do **not** need to pre-seed any register
   * (e.g. the link register) with the crash/current PC before iterating;
   * @p reg_ctx only needs to reflect the real register values of the frame
   * being unwound.
   */
  constexpr frame_pointer_iterator(frame_unwinder_ref unwinder,
                                   register_context_ref reg_ctx,
                                   uintptr_t initial_fp,
                                   uintptr_t initial_pc) noexcept
      : unwinder_(unwinder), reg_ctx_(reg_ctx),
        frame_{0, initial_fp, initial_pc},
        is_valid_(initial_fp != 0 && initial_pc != 0) {}

  [[nodiscard]] constexpr const stack_frame &
  operator*() const noexcept RELOCO_LIFETIMEBOUND {
    return frame_;
  }
  [[nodiscard]] constexpr const stack_frame *
  operator->() const noexcept RELOCO_LIFETIMEBOUND {
    return &frame_;
  }

  [[nodiscard]] constexpr bool has_value() const noexcept { return is_valid_; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return is_valid_;
  }

  /**
   * @brief Steps to the next (caller) frame using the unwinder and register
   * context.
   */
  bool next() noexcept {
    if (!is_valid_)
      return false;

    uintptr_t next_fp = 0;
    uintptr_t next_pc = 0;

    if (!unwinder_.step(reg_ctx_, frame_.pc, next_fp, next_pc)) {
      is_valid_ = false;
      return false;
    }

    frame_.frame_index++;
    frame_.fp = next_fp;
    frame_.pc = next_pc;
    return true;
  }

  frame_pointer_iterator &operator++() noexcept {
    next();
    return *this;
  }

  template <typename Visitor>
  void for_each_frame(Visitor &&visitor, uint32_t max_depth = 64) noexcept {
    while (is_valid_ && frame_.frame_index < max_depth) {
      if (!visitor(frame_)) {
        break;
      }
      next();
    }
  }

private:
  frame_unwinder_ref unwinder_{};
  register_context_ref reg_ctx_{};
  stack_frame frame_{};
  bool is_valid_{false};
};

// ============================================================================
// Backtrace View Formatter
// ============================================================================

/**
 * @brief Formattable view rendering a remote backtrace.
 */
class MICROFMT_API_CLASS RELOCO_POINTER remote_backtrace_view {
public:
  /**
   * @brief Constructs a backtrace view.
   * @param iter Cursor over the frame chain.
   * @param resolver Symbol resolver for frame PCs.
   * @param symbol_context Caller-owned symbol-resolution scratch and state.
   * @param max_depth Maximum number of frames to render.
   */
  constexpr remote_backtrace_view(frame_pointer_iterator iter,
                                  symbol_resolver_ref resolver,
                                  symbol_resolution_context &symbol_context
                                      RELOCO_LIFETIMEBOUND,
                                  uint32_t max_depth = 16) noexcept
      : iter_(iter), resolver_(resolver), symbol_context_(&symbol_context),
        max_depth_(max_depth) {}

  /**
   * @brief Returns the underlying frame cursor.
   * @return A copy of the @ref frame_pointer_iterator.
   */
  [[nodiscard]] constexpr frame_pointer_iterator iterator() const noexcept {
    return iter_;
  }
  /**
   * @brief Returns the symbol resolver handle.
   * @return Bound @ref symbol_resolver_ref.
   */
  [[nodiscard]] constexpr symbol_resolver_ref resolver() const noexcept {
    return resolver_;
  }
  [[nodiscard]] constexpr symbol_resolution_context &
  symbol_context() const noexcept RELOCO_LIFETIMEBOUND {
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
  /// Frame cursor.
  frame_pointer_iterator iter_{};
  /// Symbol resolver handle.
  symbol_resolver_ref resolver_{};
  /// Caller-owned symbol-resolution temporaries.
  value_ptr<symbol_resolution_context> symbol_context_{};
  /// Maximum number of frames to render.
  uint32_t max_depth_{16};
};

/**
 * @brief Formatter for @ref remote_backtrace_view.
 *
 * Emits one `#index fp=0x.. pc=<symbol>` line per frame. A leading `#` in the
 * specifier requests verbose symbol output.
 */
template <> struct formatter<remote_backtrace_view> {
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
   * @brief Renders the backtrace.
   * @param view The backtrace view to format.
   * @param out Destination sink.
   */
  void format(const remote_backtrace_view &view,
              const sink &out) const noexcept {
    frame_pointer_iterator cursor = view.iterator();

    bool first = true;
    cursor.for_each_frame(
        [&](const stack_frame &frame) noexcept -> bool {
          if (!first) {
            out.write("\n");
          }
          first = false;

          microfmt::format_to(out, "  #{:<2} fp={:#x}  pc=",
                              frame.frame_index, frame.fp);

          remote_fn_ptr fn_sym(frame.pc, view.resolver(),
                               view.symbol_context());
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