// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

/** @file frame_pointer.hpp @brief Type-erased frame unwinder handles, frame
 * cursor iteration, and backtrace views. */

#include "address_space.hpp"
#include "remote_diagnostics.hpp"
#include "symbol_resolver.hpp"
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace microfmt {

// ============================================================================
// Stack Frame Record
// ============================================================================

struct stack_frame {
  uint32_t frame_index{0};
  uintptr_t fp{0};
  uintptr_t pc{0};

  [[nodiscard]] constexpr bool is_null() const noexcept {
    return fp == 0 && pc == 0;
  }
};

// ============================================================================
// Customization Traits Point
// ============================================================================

template <typename ArchTag> struct frame_unwinder_traits;

// ============================================================================
// Type-Erased Unwinder Handle (2 Words)
// ============================================================================

class frame_unwinder_ref {
public:
  struct vtable {
    bool (*step)(const void *ctx, uintptr_t current_fp, uintptr_t &next_fp,
                 uintptr_t &next_pc) noexcept;
  };

  constexpr frame_unwinder_ref() noexcept = default;

  // Stateless Tag Constructor
  template <
      typename ArchTag, typename Traits = frame_unwinder_traits<ArchTag>,
      std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  constexpr explicit frame_unwinder_ref(ArchTag) noexcept
      : ctx_(nullptr), vtbl_(&s_vtbl<ArchTag>) {}

  // Stateful Tag Constructor
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

  [[nodiscard]] bool step(uintptr_t current_fp, uintptr_t &next_fp,
                          uintptr_t &next_pc) const noexcept {
    if (!vtbl_)
      return false;
    return vtbl_->step(ctx_, current_fp, next_fp, next_pc);
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

class frame_pointer_iterator {
public:
  constexpr frame_pointer_iterator() noexcept = default;

  constexpr frame_pointer_iterator(frame_unwinder_ref unwinder,
                                   uintptr_t initial_fp,
                                   uintptr_t initial_pc) noexcept
      : unwinder_(unwinder), frame_{0, initial_fp, initial_pc},
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

  // Step to the next (caller) frame
  bool next() noexcept {
    if (!is_valid_)
      return false;

    uintptr_t next_fp = 0;
    uintptr_t next_pc = 0;

    if (!unwinder_.step(frame_.fp, next_fp, next_pc)) {
      is_valid_ = false;
      return false;
    }

    frame_.frame_index++;
    frame_.fp = next_fp;
    frame_.pc = next_pc;
    return true;
  }

  // Pre-increment syntax support
  frame_pointer_iterator &operator++() noexcept {
    next();
    return *this;
  }

  // Visitor-style traversal
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
  stack_frame frame_{};
  bool is_valid_{false};
};

// ============================================================================
// Backtrace View Formatter
// ============================================================================

class remote_backtrace_view {
public:
  constexpr remote_backtrace_view(frame_pointer_iterator iter,
                                  symbol_resolver_ref resolver,
                                  span<char> scratch,
                                  uint32_t max_depth = 16) noexcept
      : iter_(iter), resolver_(resolver), scratch_(scratch),
        max_depth_(max_depth) {}

  template <size_t N>
  constexpr remote_backtrace_view(frame_pointer_iterator iter,
                                  symbol_resolver_ref resolver,
                                  char (&scratch)[N],
                                  uint32_t max_depth = 16) noexcept
      : iter_(iter), resolver_(resolver), scratch_(scratch, N),
        max_depth_(max_depth) {}

  [[nodiscard]] constexpr frame_pointer_iterator iterator() const noexcept {
    return iter_;
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
  frame_pointer_iterator iter_{};
  symbol_resolver_ref resolver_{};
  span<char> scratch_{};
  uint32_t max_depth_{16};
};

template <> struct formatter<remote_backtrace_view> {
  char mode{'\0'};

  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    if (!spec.empty() && spec.front() == '#') {
      mode = '#';
    }
  }

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