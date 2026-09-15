// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

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
struct stack_frame {
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
class frame_unwinder_ref {
public:
  /**
   * @brief Virtual table of unwinder operations.
   */
  struct vtable {
    /**
     * @brief Advances one frame using register context.
     */
    bool (*step)(const void *ctx, register_context_ref reg_ctx,
                 uintptr_t &next_fp, uintptr_t &next_pc) noexcept;
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
  constexpr frame_unwinder_ref(ArchTag, const Context &ctx) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<ArchTag>) {}

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
  make(const Context &ctx) noexcept {
    return frame_unwinder_ref(ArchTag{}, ctx);
  }

  /**
   * @brief Advances from the current frame to its caller using
   * register_context_ref.
   */
  [[nodiscard]] bool step(register_context_ref reg_ctx, uintptr_t &next_fp,
                          uintptr_t &next_pc) const noexcept {
    if (!vtbl_)
      return false;
    return vtbl_->step(ctx_, reg_ctx, next_fp, next_pc);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return vtbl_ != nullptr;
  }

private:
  template <typename ArchTag>
  static constexpr vtable s_vtbl{&frame_unwinder_traits<ArchTag>::step};

  const void *ctx_{nullptr};
  const vtable *vtbl_{nullptr};
};

// ============================================================================
// Type-Erased Frame Pointer Iterator / Cursor
// ============================================================================

/**
 * @brief Forward-only cursor iterating over a stack walk backed by register
 * context.
 */
class frame_pointer_iterator {
public:
  constexpr frame_pointer_iterator() noexcept = default;

  /**
   * @brief Constructs an iterator rooted at the given frame and register
   * context.
   */
  constexpr frame_pointer_iterator(frame_unwinder_ref unwinder,
                                   register_context_ref reg_ctx,
                                   uintptr_t initial_fp,
                                   uintptr_t initial_pc) noexcept
      : unwinder_(unwinder), reg_ctx_(reg_ctx),
        frame_{0, initial_fp, initial_pc},
        is_valid_(initial_fp != 0 && initial_pc != 0) {}

  [[nodiscard]] constexpr const stack_frame &operator*() const noexcept {
    return frame_;
  }
  [[nodiscard]] constexpr const stack_frame *operator->() const noexcept {
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

    if (!unwinder_.step(reg_ctx_, next_fp, next_pc)) {
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
class remote_backtrace_view {
public:
  /**
   * @brief Constructs a backtrace view.
   * @param iter Cursor over the frame chain.
   * @param resolver Symbol resolver for frame PCs.
   * @param scratch Scratch buffer for symbol strings.
   * @param max_depth Maximum number of frames to render.
   */
  constexpr remote_backtrace_view(frame_pointer_iterator iter,
                                  symbol_resolver_ref resolver,
                                  span<char> scratch,
                                  uint32_t max_depth = 16) noexcept
      : iter_(iter), resolver_(resolver), scratch_(scratch),
        max_depth_(max_depth) {}

  /**
   * @brief Constructs a backtrace view with a C-array scratch buffer.
   * @tparam N Scratch buffer size.
   * @param iter Cursor over the frame chain.
   * @param resolver Symbol resolver for frame PCs.
   * @param scratch Scratch buffer for symbol strings.
   * @param max_depth Maximum number of frames to render.
   */
  template <size_t N>
  constexpr remote_backtrace_view(frame_pointer_iterator iter,
                                  symbol_resolver_ref resolver,
                                  char (&scratch)[N],
                                  uint32_t max_depth = 16) noexcept
      : iter_(iter), resolver_(resolver), scratch_(scratch, N),
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
  /// Frame cursor.
  frame_pointer_iterator iter_{};
  /// Symbol resolver handle.
  symbol_resolver_ref resolver_{};
  /// Scratch span for symbol strings.
  span<char> scratch_{};
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

          microfmt::format_to(out, MICROFMT_STRING("  #{:<2} fp={:#x}  pc="),
                              frame.frame_index, frame.fp);

          remote_fn_ptr fn_sym(frame.pc, view.resolver(), view.scratch());
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