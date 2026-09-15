// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file exception_frame.hpp @brief Trap/exception frame decoding, iteration,
 * and formattable trap summaries. */

#include "address_space.hpp"
#include "frame_pointer.hpp"
#include "symbol_resolver.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace microfmt {

// ============================================================================
// Normalized Trap Context Snapshot
// ============================================================================

/**
 * @brief Normalized, architecture-independent snapshot of a trap context.
 */
struct trap_context {
  /**
   * @brief Nested trap level (`0` = primary/top fault).
   */
  uint32_t trap_level{0}; // 0 = primary/top fault, 1 = nested exception, etc.
  /**
   * @brief Address of the raw architecture trap frame.
   */
  uintptr_t trap_frame_addr{0}; // Address of the raw architecture pt_regs/frame
  /**
   * @brief Faulting instruction pointer (RIP/PC/ELR).
   */
  uintptr_t pc{0}; // Faulting instruction pointer (RIP/PC/ELR)
  /**
   * @brief Faulting stack pointer (RSP/SP).
   */
  uintptr_t sp{0}; // Faulting stack pointer (RSP/SP)
  /**
   * @brief Faulting frame pointer (RBP/FP/x29).
   */
  uintptr_t fp{0}; // Faulting frame pointer (RBP/FP/x29)
  /**
   * @brief Link register (AArch64/ARM32), or `0` on x86.
   */
  uintptr_t lr{0}; // Link register (AArch64/ARM32) or 0 (x86)
  /**
   * @brief Vector number, trap error code, or exception syndrome register.
   */
  uint64_t vector_or_reason{0}; // Vector number, trap error code, or ESR
  /**
   * @brief `true` when the trap originated in user mode.
   */
  bool is_user_mode{false}; // True if origin was user mode
};

// ============================================================================
// Customization Point (exception_frame_traits)
// ============================================================================

/**
 * @brief Static customization point for decoding architecture trap frames.
 * @tparam ArchTag Tag identifying the architecture.
 */
template <typename ArchTag> struct exception_frame_traits;

// ============================================================================
// Type-Erased Exception Frame Accessor (2 Words)
// ============================================================================

/**
 * @brief Type-erased, two-word handle decoding exception/trap frames.
 */
class exception_frame_ref {
public:
  /**
   * @brief Virtual table of trap-frame operations.
   */
  struct vtable {
    /**
     * @brief Decodes a raw trap frame. See @ref exception_frame_ref::decode.
     */
    bool (*decode)(const void *ctx, uintptr_t trap_frame_addr,
                   trap_context &out_trap) noexcept;
    /**
     * @brief Finds the next nested trap frame. See
     * @ref exception_frame_ref::next_trap_frame.
     */
    bool (*next_trap_frame)(const void *ctx, uintptr_t current_trap_frame_addr,
                            uintptr_t &next_trap_frame_addr) noexcept;
    /**
     * @brief Describes a vector/reason code. See
     * @ref exception_frame_ref::describe_reason.
     */
    microfmt::string_view (*describe_reason)(const void *ctx,
                                        uint64_t vector_or_reason) noexcept;
  };

  /**
   * @brief Constructs an empty (invalid) handle.
   */
  constexpr exception_frame_ref() noexcept = default;

  /**
   * @brief Constructs a handle for a stateless architecture tag.
   * @tparam ArchTag Architecture tag type.
   * @tparam Traits Specialized traits, enabled when `context_type` is `void`.
   */
  template <
      typename ArchTag, typename Traits = exception_frame_traits<ArchTag>,
      std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  constexpr explicit exception_frame_ref(ArchTag) noexcept
      : ctx_(nullptr), vtbl_(&s_vtbl<ArchTag>) {}

  /**
   * @brief Constructs a handle for a stateful architecture tag.
   * @tparam ArchTag Architecture tag type.
   * @tparam Context Concrete context type.
   * @tparam Traits Specialized traits, enabled when `context_type` is non-void
   * and @p Context converts to it.
   * @param ctx Context object decoding trap frames.
   */
  template <typename ArchTag, typename Context,
            typename Traits = exception_frame_traits<ArchTag>,
            std::enable_if_t<
                !std::is_void_v<typename Traits::context_type> &&
                    std::is_convertible_v<
                        const Context *, const typename Traits::context_type *>,
                int> = 0>
  constexpr exception_frame_ref(ArchTag, const Context &ctx) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<ArchTag>) {}

  /**
   * @brief Creates a handle for a stateless architecture tag.
   * @tparam ArchTag Architecture tag type.
   * @tparam Traits Specialized traits, enabled when `context_type` is `void`.
   * @return An @ref exception_frame_ref for the tag.
   */
  template <
      typename ArchTag, typename Traits = exception_frame_traits<ArchTag>,
      std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr exception_frame_ref make() noexcept {
    return exception_frame_ref(ArchTag{});
  }

  /**
   * @brief Creates a handle for a stateful architecture tag.
   * @tparam ArchTag Architecture tag type.
   * @tparam Context Concrete context type.
   * @tparam Traits Specialized traits, enabled when `context_type` is non-void.
   * @param ctx Context object decoding trap frames.
   * @return An @ref exception_frame_ref bound to @p ctx.
   */
  template <
      typename ArchTag, typename Context,
      typename Traits = exception_frame_traits<ArchTag>,
      std::enable_if_t<!std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr exception_frame_ref
  make(const Context &ctx) noexcept {
    return exception_frame_ref(ArchTag{}, ctx);
  }

  /**
   * @brief Decodes raw trap frame memory into a uniform @ref trap_context.
   * @param trap_frame_addr Address of the raw trap frame.
   * @param out_trap Receives the decoded, normalized snapshot.
   * @return `true` on success, `false` when the handle is empty or decoding
   * fails.
   */
  [[nodiscard]] bool decode(uintptr_t trap_frame_addr,
                            trap_context &out_trap) const noexcept {
    if (!vtbl_)
      return false;
    return vtbl_->decode(ctx_, trap_frame_addr, out_trap);
  }

  /**
   * @brief Follows links to outer/nested trap frames.
   * @param current_trap_frame_addr Address of the current trap frame.
   * @param next_trap_frame_addr Receives the next trap frame address.
   * @return `true` when a next frame was found.
   */
  [[nodiscard]] bool
  next_trap_frame(uintptr_t current_trap_frame_addr,
                  uintptr_t &next_trap_frame_addr) const noexcept {
    if (!vtbl_)
      return false;
    return vtbl_->next_trap_frame(ctx_, current_trap_frame_addr,
                                  next_trap_frame_addr);
  }

  /**
   * @brief Returns a textual description of a vector/reason code.
   * @param vector_or_reason Vector number or reason code.
   * @return Human-readable description, or an empty view when unavailable.
   */
  [[nodiscard]] microfmt::string_view
  describe_reason(uint64_t vector_or_reason) const noexcept {
    if (!vtbl_ || !vtbl_->describe_reason)
      return {};
    return vtbl_->describe_reason(ctx_, vector_or_reason);
  }

  /**
   * @brief Reports whether the handle is bound to a decoder.
   * @return `true` when the handle is valid.
   */
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return vtbl_ != nullptr;
  }

private:
  template <typename ArchTag>
  static constexpr vtable s_vtbl{
      &exception_frame_traits<ArchTag>::decode,
      &exception_frame_traits<ArchTag>::next_trap_frame,
      [](const void *ctx, uint64_t vector_or_reason) noexcept {
        return microfmt::string_view(
            exception_frame_traits<ArchTag>::describe_reason(
                ctx, vector_or_reason));
      }};

  const void *ctx_{nullptr};
  const vtable *vtbl_{nullptr};
};

// ============================================================================
// Type-Erased Exception Frame Iterator
// ============================================================================

/**
 * @brief Forward-only cursor iterating over a chain of trap frames.
 */
class exception_frame_iterator {
public:
  /**
   * @brief Constructs an empty (invalid) iterator.
   */
  constexpr exception_frame_iterator() noexcept = default;

  /**
   * @brief Constructs an iterator rooted at the given trap frame.
   * @param decoder Trap-frame decoder driving the iteration.
   * @param initial_trap_frame_addr Address of the first trap frame.
   */
  constexpr exception_frame_iterator(exception_frame_ref decoder,
                                     uintptr_t initial_trap_frame_addr) noexcept
      : decoder_(decoder), raw_frame_addr_(initial_trap_frame_addr),
        is_valid_(initial_trap_frame_addr != 0) {
    if (is_valid_) {
      load_current();
    }
  }

  /**
   * @brief Returns the current decoded trap context.
   * @return Reference to the current @ref trap_context.
   */
  [[nodiscard]] constexpr const trap_context &operator*() const noexcept {
    return current_;
  }
  /**
   * @brief Returns the current decoded trap context.
   * @return Pointer to the current @ref trap_context.
   */
  [[nodiscard]] constexpr const trap_context *operator->() const noexcept {
    return &current_;
  }

  /**
   * @brief Reports whether the iterator is positioned on a trap frame.
   * @return `true` while the current frame is valid.
   */
  [[nodiscard]] constexpr bool has_value() const noexcept { return is_valid_; }
  /**
   * @brief Reports whether the iterator is positioned on a trap frame.
   * @return `true` while the current frame is valid.
   */
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return is_valid_;
  }

  /**
   * @brief Advances to the next (outer/nested) trap frame.
   * @return `false` when the chain ends.
   */
  bool next() noexcept {
    if (!is_valid_)
      return false;

    uintptr_t next_raw_addr = 0;
    if (!decoder_.next_trap_frame(raw_frame_addr_, next_raw_addr) ||
        next_raw_addr == 0) {
      is_valid_ = false;
      return false;
    }

    raw_frame_addr_ = next_raw_addr;
    current_level_++;
    return load_current();
  }

  /**
   * @brief Pre-increment advancing to the next trap frame.
   * @return This iterator.
   */
  exception_frame_iterator &operator++() noexcept {
    next();
    return *this;
  }

  /**
   * @brief Visitor-style traversal over the trap chain.
   * @tparam Visitor Callable accepting `const trap_context &` and returning
   * `bool` (`true` = continue).
   * @param visitor Visitor invoked per trap frame.
   * @param max_traps Maximum trap level to visit.
   */
  template <typename Visitor>
  void for_each_trap(Visitor &&visitor, uint32_t max_traps = 8) noexcept {
    while (is_valid_ && current_.trap_level < max_traps) {
      if (!visitor(current_)) {
        break;
      }
      next();
    }
  }

private:
  bool load_current() noexcept {
    if (!decoder_.decode(raw_frame_addr_, current_)) {
      is_valid_ = false;
      return false;
    }
    current_.trap_level = current_level_;
    current_.trap_frame_addr = raw_frame_addr_;
    return true;
  }

  /// Trap-frame decoder.
  exception_frame_ref decoder_{};
  /// Raw address of the current trap frame.
  uintptr_t raw_frame_addr_{};
  /// Current decoded trap context.
  trap_context current_{};
  /// Current nesting level.
  uint32_t current_level_{0};
  /// Validity flag.
  bool is_valid_{false};
};

// ============================================================================
// Formattable Exception & Trap Summary View
// ============================================================================

/**
 * @brief Formattable summary view of a trap context.
 */
class remote_trap_view {
public:
  /**
   * @brief Constructs a trap summary view.
   * @param trap The decoded trap context to summarize.
   * @param decoder Trap-frame decoder (used for reason descriptions).
   * @param resolver Symbol resolver for PCs.
   * @param scratch Scratch buffer for symbol strings.
   */
  constexpr remote_trap_view(const trap_context &trap,
                             exception_frame_ref decoder,
                             symbol_resolver_ref resolver,
                             span<char> scratch) noexcept
      : trap_(trap), decoder_(decoder), resolver_(resolver), scratch_(scratch) {
  }

  /**
   * @brief Constructs a trap summary view with a C-array scratch buffer.
   * @tparam N Scratch buffer size.
   * @param trap The decoded trap context to summarize.
   * @param decoder Trap-frame decoder (used for reason descriptions).
   * @param resolver Symbol resolver for PCs.
   * @param scratch Scratch buffer for symbol strings.
   */
  template <size_t N>
  constexpr remote_trap_view(const trap_context &trap,
                             exception_frame_ref decoder,
                             symbol_resolver_ref resolver,
                             char (&scratch)[N]) noexcept
      : trap_(trap), decoder_(decoder), resolver_(resolver),
        scratch_(scratch, N) {}

  /**
   * @brief Returns the wrapped trap context.
   * @return Reference to the @ref trap_context.
   */
  [[nodiscard]] constexpr const trap_context &trap() const noexcept {
    return trap_;
  }
  /**
   * @brief Returns the trap-frame decoder handle.
   * @return Bound @ref exception_frame_ref.
   */
  [[nodiscard]] constexpr exception_frame_ref decoder() const noexcept {
    return decoder_;
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

private:
  /// Wrapped trap context.
  trap_context trap_{};
  /// Trap-frame decoder handle.
  exception_frame_ref decoder_{};
  /// Symbol resolver handle.
  symbol_resolver_ref resolver_{};
  /// Scratch span for symbol strings.
  span<char> scratch_{};
};

/**
 * @brief Formatter for @ref remote_trap_view.
 *
 * Emits a multi-line summary with the trap level, mode, reason, PC, SP, FP,
 * and optionally LR. A leading `#` in the specifier requests verbose symbol
 * output.
 */
template <> struct formatter<remote_trap_view> {
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
   * @brief Renders the trap summary.
   * @param view The trap view to format.
   * @param out Destination sink.
   */
  void format(const remote_trap_view &view, const sink &out) const noexcept {
    const auto &trap = view.trap();
    microfmt::string_view desc =
        view.decoder().describe_reason(trap.vector_or_reason);

    microfmt::format_to(
        out, MICROFMT_STRING("[Trap Level {} @ {:#x}] Mode: {}\n"),
        trap.trap_level, trap.trap_frame_addr,
        trap.is_user_mode ? "User" : "Kernel");

    if (!desc.empty()) {
      microfmt::format_to(out, MICROFMT_STRING("  Reason : {} ({:#x})\n"), desc,
                          trap.vector_or_reason);
    } else if (trap.vector_or_reason != 0) {
      microfmt::format_to(out, MICROFMT_STRING("  Vector : {:#x}\n"),
                          trap.vector_or_reason);
    }

    microfmt::format_to(out, MICROFMT_STRING("  PC     : "));
    remote_fn_ptr pc_sym(trap.pc, view.resolver(), view.scratch());
    if (mode == '#') {
      microfmt::format_to(out, MICROFMT_STRING("{:#}\n"), pc_sym);
    } else {
      microfmt::format_to(out, MICROFMT_STRING("{}\n"), pc_sym);
    }

    microfmt::format_to(out, MICROFMT_STRING("  SP     : {:#x}\n"), trap.sp);
    microfmt::format_to(out, MICROFMT_STRING("  FP     : {:#x}"), trap.fp);
    if (trap.lr != 0) {
      microfmt::format_to(out, MICROFMT_STRING("\n  LR     : "));
      remote_fn_ptr lr_sym(trap.lr, view.resolver(), view.scratch());
      microfmt::format_to(out, MICROFMT_STRING("{}"), lr_sym);
    }
  }
};

} // namespace microfmt