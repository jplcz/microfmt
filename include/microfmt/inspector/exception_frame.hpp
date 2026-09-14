// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

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

struct trap_context {
  uint32_t trap_level{0}; // 0 = primary/top fault, 1 = nested exception, etc.
  uintptr_t trap_frame_addr{0}; // Address of the raw architecture pt_regs/frame
  uintptr_t pc{0};              // Faulting instruction pointer (RIP/PC/ELR)
  uintptr_t sp{0};              // Faulting stack pointer (RSP/SP)
  uintptr_t fp{0};              // Faulting frame pointer (RBP/FP/x29)
  uintptr_t lr{0};              // Link register (AArch64/ARM32) or 0 (x86)
  uint64_t vector_or_reason{0}; // Vector number, trap error code, or ESR
  bool is_user_mode{false};     // True if origin was user mode
};

// ============================================================================
// Customization Point (exception_frame_traits)
// ============================================================================

template <typename ArchTag> struct exception_frame_traits;

// ============================================================================
// Type-Erased Exception Frame Accessor (2 Words)
// ============================================================================

class exception_frame_ref {
public:
  struct vtable {
    bool (*decode)(const void *ctx, uintptr_t trap_frame_addr,
                   trap_context &out_trap) noexcept;
    bool (*next_trap_frame)(const void *ctx, uintptr_t current_trap_frame_addr,
                            uintptr_t &next_trap_frame_addr) noexcept;
    std::string_view (*describe_reason)(const void *ctx,
                                        uint64_t vector_or_reason) noexcept;
  };

  constexpr exception_frame_ref() noexcept = default;

  // Stateless Tag constructor
  template <
      typename ArchTag, typename Traits = exception_frame_traits<ArchTag>,
      std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  constexpr explicit exception_frame_ref(ArchTag) noexcept
      : ctx_(nullptr), vtbl_(&s_vtbl<ArchTag>) {}

  // Stateful Tag constructor
  template <typename ArchTag, typename Context,
            typename Traits = exception_frame_traits<ArchTag>,
            std::enable_if_t<
                !std::is_void_v<typename Traits::context_type> &&
                    std::is_convertible_v<
                        const Context *, const typename Traits::context_type *>,
                int> = 0>
  constexpr exception_frame_ref(ArchTag, const Context &ctx) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<ArchTag>) {}

  template <
      typename ArchTag, typename Traits = exception_frame_traits<ArchTag>,
      std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr exception_frame_ref make() noexcept {
    return exception_frame_ref(ArchTag{});
  }

  template <
      typename ArchTag, typename Context,
      typename Traits = exception_frame_traits<ArchTag>,
      std::enable_if_t<!std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr exception_frame_ref
  make(const Context &ctx) noexcept {
    return exception_frame_ref(ArchTag{}, ctx);
  }

  // Decodes raw trap frame memory into the uniform trap_context
  [[nodiscard]] bool decode(uintptr_t trap_frame_addr,
                            trap_context &out_trap) const noexcept {
    if (!vtbl_)
      return false;
    return vtbl_->decode(ctx_, trap_frame_addr, out_trap);
  }

  // Follows links to outer/nested trap frames (e.g., through thread->trap_frame
  // or stack markers)
  [[nodiscard]] bool
  next_trap_frame(uintptr_t current_trap_frame_addr,
                  uintptr_t &next_trap_frame_addr) const noexcept {
    if (!vtbl_)
      return false;
    return vtbl_->next_trap_frame(ctx_, current_trap_frame_addr,
                                  next_trap_frame_addr);
  }

  [[nodiscard]] std::string_view
  describe_reason(uint64_t vector_or_reason) const noexcept {
    if (!vtbl_ || !vtbl_->describe_reason)
      return {};
    return vtbl_->describe_reason(ctx_, vector_or_reason);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return vtbl_ != nullptr;
  }

private:
  template <typename ArchTag>
  static constexpr vtable s_vtbl{
      &exception_frame_traits<ArchTag>::decode,
      &exception_frame_traits<ArchTag>::next_trap_frame,
      &exception_frame_traits<ArchTag>::describe_reason};

  const void *ctx_{nullptr};
  const vtable *vtbl_{nullptr};
};

// ============================================================================
// Type-Erased Exception Frame Iterator
// ============================================================================

class exception_frame_iterator {
public:
  constexpr exception_frame_iterator() noexcept = default;

  constexpr exception_frame_iterator(exception_frame_ref decoder,
                                     uintptr_t initial_trap_frame_addr) noexcept
      : decoder_(decoder), raw_frame_addr_(initial_trap_frame_addr),
        is_valid_(initial_trap_frame_addr != 0) {
    if (is_valid_) {
      load_current();
    }
  }

  [[nodiscard]] constexpr const trap_context &operator*() const noexcept {
    return current_;
  }
  [[nodiscard]] constexpr const trap_context *operator->() const noexcept {
    return &current_;
  }

  [[nodiscard]] constexpr bool has_value() const noexcept { return is_valid_; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return is_valid_;
  }

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

  exception_frame_iterator &operator++() noexcept {
    next();
    return *this;
  }

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

  exception_frame_ref decoder_{};
  uintptr_t raw_frame_addr_{0};
  trap_context current_{};
  uint32_t current_level_{0};
  bool is_valid_{false};
};

// ============================================================================
// Formattable Exception & Trap Summary View
// ============================================================================

class remote_trap_view {
public:
  constexpr remote_trap_view(const trap_context &trap,
                             exception_frame_ref decoder,
                             symbol_resolver_ref resolver,
                             span<char> scratch) noexcept
      : trap_(trap), decoder_(decoder), resolver_(resolver), scratch_(scratch) {
  }

  template <size_t N>
  constexpr remote_trap_view(const trap_context &trap,
                             exception_frame_ref decoder,
                             symbol_resolver_ref resolver,
                             char (&scratch)[N]) noexcept
      : trap_(trap), decoder_(decoder), resolver_(resolver),
        scratch_(scratch, N) {}

  [[nodiscard]] constexpr const trap_context &trap() const noexcept {
    return trap_;
  }
  [[nodiscard]] constexpr exception_frame_ref decoder() const noexcept {
    return decoder_;
  }
  [[nodiscard]] constexpr symbol_resolver_ref resolver() const noexcept {
    return resolver_;
  }
  [[nodiscard]] constexpr span<char> scratch() const noexcept {
    return scratch_;
  }

private:
  trap_context trap_{};
  exception_frame_ref decoder_{};
  symbol_resolver_ref resolver_{};
  span<char> scratch_{};
};

template <> struct formatter<remote_trap_view> {
  char mode{'\0'};

  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    if (!spec.empty() && spec.front() == '#') {
      mode = '#';
    }
  }

  void format(const remote_trap_view &view, const sink &out) const noexcept {
    const auto &trap = view.trap();
    std::string_view desc =
        view.decoder().describe_reason(trap.vector_or_reason);

    microfmt::format_to(out, "[Trap Level {} @ {:#x}] Mode: {}\n",
                        trap.trap_level, trap.trap_frame_addr,
                        trap.is_user_mode ? "User" : "Kernel");

    if (!desc.empty()) {
      microfmt::format_to(out, "  Reason : {} ({:#x})\n", desc,
                          trap.vector_or_reason);
    } else if (trap.vector_or_reason != 0) {
      microfmt::format_to(out, "  Vector : {:#x}\n", trap.vector_or_reason);
    }

    microfmt::format_to(out, "  PC     : ");
    remote_fn_ptr pc_sym(trap.pc, view.resolver(), view.scratch());
    if (mode == '#') {
      microfmt::format_to(out, "{:#}\n", pc_sym);
    } else {
      microfmt::format_to(out, "{}\n", pc_sym);
    }

    microfmt::format_to(out, "  SP     : {:#x}\n", trap.sp);
    microfmt::format_to(out, "  FP     : {:#x}", trap.fp);
    if (trap.lr != 0) {
      microfmt::format_to(out, "\n  LR     : ");
      remote_fn_ptr lr_sym(trap.lr, view.resolver(), view.scratch());
      microfmt::format_to(out, "{}", lr_sym);
    }
  }
};

} // namespace microfmt