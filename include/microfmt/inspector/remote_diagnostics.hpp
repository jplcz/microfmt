// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

/** @file remote_diagnostics.hpp @brief Remote fault formatting and
 * diagnostics-aware remote pointer/struct views. */

#include "address_space.hpp"
#include "symbol_resolver.hpp"
#include <cstdint>
#include <string_view>

namespace microfmt {

// ============================================================================
// Diagnostic Formatter for Faults
// ============================================================================

inline void format_remote_fault(const sink &out, uintptr_t addr,
                                symbol_resolver_ref resolver,
                                span<char> scratch) noexcept {
  if (addr == 0) {
    out.write("<fault:nullptr>");
    return;
  }

  if (resolver) {
    resolved_symbol_info info{};
    if (resolver.resolve(addr, scratch, info) &&
        (info.has_symbol() || info.has_image())) {
      out.write("<fault@");

      if (info.has_symbol()) {
        if (info.has_image()) {
          microfmt::format_to(out, "{}!", info.image_name);
        }
        microfmt::format_to(out, "{}", as_demangled(info.symbol_name));
        if (info.offset_from_symbol > 0) {
          microfmt::format_to(out, "+{:#x}", info.offset_from_symbol);
        }
      } else {
        // Module known, symbol stripped: e.g. "faulty_driver.ko+0x4200"
        microfmt::format_to(out, "{}+{:#x}", info.image_name,
                            info.offset_from_image);
      }

      out.write(">");
      return;
    }
  }

  // Fallback raw hex address
  microfmt::format_to(out, "<fault:{:#x}>", addr);
}

// ============================================================================
// Symbolic Remote Function / Code Pointer View
// ============================================================================

class remote_fn_ptr {
public:
  constexpr remote_fn_ptr() noexcept = default;

  constexpr remote_fn_ptr(uintptr_t addr, symbol_resolver_ref resolver,
                          span<char> scratch) noexcept
      : addr_(addr), resolver_(resolver), scratch_(scratch) {}

  template <size_t N>
  constexpr remote_fn_ptr(uintptr_t addr, symbol_resolver_ref resolver,
                          char (&scratch)[N]) noexcept
      : addr_(addr), resolver_(resolver), scratch_(scratch, N) {}

  [[nodiscard]] constexpr uintptr_t address() const noexcept { return addr_; }
  [[nodiscard]] constexpr bool is_null() const noexcept { return addr_ == 0; }
  [[nodiscard]] constexpr symbol_resolver_ref resolver() const noexcept {
    return resolver_;
  }
  [[nodiscard]] constexpr span<char> scratch() const noexcept {
    return scratch_;
  }

private:
  uintptr_t addr_{0};
  symbol_resolver_ref resolver_{};
  span<char> scratch_{};
};

template <> struct formatter<remote_fn_ptr> {
  char mode{'\0'};

  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    if (!spec.empty()) {
      mode = spec.front();
    }
  }

  void format(const remote_fn_ptr &fn, const sink &out) const noexcept {
    if (fn.is_null()) {
      out.write("(null)");
      return;
    }

    if (mode == 'x' || mode == 'p' || !fn.resolver()) {
      microfmt::format_to(out, "{:#x}", fn.address());
      return;
    }

    auto sym_view =
        remote_symbol_view(fn.address(), fn.resolver(), fn.scratch(), true);
    if (mode == '#') {
      microfmt::format_to(out, "{:#}", sym_view);
    } else {
      microfmt::format_to(out, "{}", sym_view);
    }
  }
};

// ============================================================================
// Diagnostics-Aware Remote Struct View
// ============================================================================

template <typename T> class remote_diag_ref {
public:
  constexpr remote_diag_ref(uintptr_t addr, address_space_ref space,
                            symbol_resolver_ref resolver,
                            span<std::byte> scratch,
                            span<char> str_scratch = {}) noexcept
      : addr_(addr), space_(space), resolver_(resolver), scratch_(scratch),
        str_scratch_(str_scratch) {}

  [[nodiscard]] constexpr uintptr_t address() const noexcept { return addr_; }
  [[nodiscard]] constexpr bool is_null() const noexcept { return addr_ == 0; }

  [[nodiscard]] bool load(T *&out_ptr) const noexcept {
    if (scratch_.size() < sizeof(T))
      return false;
    if (reinterpret_cast<uintptr_t>(scratch_.data()) % alignof(T) != 0)
      return false;

    out_ptr = reinterpret_cast<T *>(scratch_.data());
    return space_.read_bytes(addr_, out_ptr, sizeof(T));
  }

  [[nodiscard]] constexpr address_space_ref space() const noexcept {
    return space_;
  }
  [[nodiscard]] constexpr symbol_resolver_ref resolver() const noexcept {
    return resolver_;
  }
  [[nodiscard]] constexpr span<char> str_scratch() const noexcept {
    return str_scratch_;
  }

private:
  uintptr_t addr_{0};
  address_space_ref space_{};
  symbol_resolver_ref resolver_{};
  span<std::byte> scratch_{};
  span<char> str_scratch_{};
};

template <typename T> struct formatter<remote_diag_ref<T>> {
  std::string_view spec_{""};

  constexpr void parse(format_parse_context &ctx) noexcept {
    spec_ = ctx.spec();
  }

  void format(const remote_diag_ref<T> &view, const sink &out) const noexcept {
    if (view.is_null()) {
      out.write("(null)");
      return;
    }

    T *staged = nullptr;
    if (!view.load(staged)) {
      format_remote_fault(out, view.address(), view.resolver(),
                          view.str_scratch());
      return;
    }

    formatter<T> elem_fmt;
    format_parse_context pctx(spec_);
    elem_fmt.parse(pctx);
    elem_fmt.format(*staged, out);
  }
};

} // namespace microfmt
