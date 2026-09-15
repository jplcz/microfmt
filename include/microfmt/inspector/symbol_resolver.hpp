// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

/** @file symbol_resolver.hpp @brief Type-erased symbol resolution for remote
 * addresses and formattable symbol views. */

#include "address_space.hpp"
#include "demangle.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace microfmt {

// ============================================================================
// Raw Kernel / Platform Resolution Result
// ============================================================================

struct raw_resolved_symbol {
  std::string_view symbol_name{""};
  uintptr_t symbol_base{0};
  bool is_exact{false};

  std::string_view image_name{""};
  uintptr_t image_load_base{0};
};

// ============================================================================
// High-Level Derived Symbol Info
// ============================================================================

struct resolved_symbol_info {
  std::string_view symbol_name{""};
  uintptr_t symbol_base{0};
  uintptr_t offset_from_symbol{0};
  bool is_exact{false};

  std::string_view image_name{""};
  uintptr_t image_load_base{0};
  uintptr_t offset_from_image{0};

  [[nodiscard]] constexpr bool has_symbol() const noexcept {
    return !symbol_name.empty();
  }

  [[nodiscard]] constexpr bool has_image() const noexcept {
    return !image_name.empty();
  }
};

// ============================================================================
// Static Customization Point: symbol_resolver_traits
// ============================================================================

template <typename Tag> struct symbol_resolver_traits;

// ============================================================================
// Minimal-Stack Type-Erased Symbol Resolver (2 Words)
// ============================================================================

class symbol_resolver_ref {
public:
  struct vtable {
    bool (*resolve)(const void *ctx, uintptr_t addr, span<char> scratch,
                    raw_resolved_symbol &out_raw) noexcept;
  };

  constexpr symbol_resolver_ref() noexcept = default;

  // Stateless Tag constructor
  template <
      typename Tag, typename Traits = symbol_resolver_traits<Tag>,
      std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  constexpr explicit symbol_resolver_ref(Tag) noexcept
      : ctx_(nullptr), vtbl_(&s_vtbl<Tag>) {}

  // Stateful Tag constructor
  template <typename Tag, typename Context,
            typename Traits = symbol_resolver_traits<Tag>,
            std::enable_if_t<
                !std::is_void_v<typename Traits::context_type> &&
                    std::is_convertible_v<
                        const Context *, const typename Traits::context_type *>,
                int> = 0>
  constexpr symbol_resolver_ref(Tag, const Context &ctx) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag>) {}

  template <
      typename Tag, typename Traits = symbol_resolver_traits<Tag>,
      std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr symbol_resolver_ref make() noexcept {
    return symbol_resolver_ref(Tag{});
  }

  template <
      typename Tag, typename Context,
      typename Traits = symbol_resolver_traits<Tag>,
      std::enable_if_t<!std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr symbol_resolver_ref
  make(const Context &ctx) noexcept {
    return symbol_resolver_ref(Tag{}, ctx);
  }

  // Resolves address and calculates derived offsets
  [[nodiscard]] bool resolve(uintptr_t addr, span<char> scratch,
                             resolved_symbol_info &out_info) const noexcept {
    if (!vtbl_)
      return false;

    raw_resolved_symbol raw{};
    if (!vtbl_->resolve(ctx_, addr, scratch, raw)) {
      return false;
    }

    out_info.symbol_name = raw.symbol_name;
    out_info.symbol_base = raw.symbol_base;
    out_info.is_exact = raw.is_exact;
    out_info.image_name = raw.image_name;
    out_info.image_load_base = raw.image_load_base;

    // Derive relative offsets
    if (raw.symbol_base != 0 && addr >= raw.symbol_base) {
      out_info.offset_from_symbol = addr - raw.symbol_base;
    } else {
      out_info.offset_from_symbol = 0;
    }

    if (raw.image_load_base != 0 && addr >= raw.image_load_base) {
      out_info.offset_from_image = addr - raw.image_load_base;
    } else {
      out_info.offset_from_image = 0;
    }

    return true;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return vtbl_ != nullptr;
  }

private:
  template <typename Tag>
  static constexpr vtable s_vtbl{&symbol_resolver_traits<Tag>::resolve};

  const void *ctx_{nullptr};
  const vtable *vtbl_{nullptr};
};

// ============================================================================
// Remote Symbol View & Formatter
// ============================================================================

class remote_symbol_view {
public:
  constexpr remote_symbol_view() noexcept = default;

  constexpr remote_symbol_view(uintptr_t addr, symbol_resolver_ref resolver,
                               span<char> scratch,
                               bool demangle = true) noexcept
      : addr_(addr), resolver_(resolver), scratch_(scratch),
        demangle_(demangle) {}

  template <size_t N>
  constexpr remote_symbol_view(uintptr_t addr, symbol_resolver_ref resolver,
                               char (&scratch)[N],
                               bool demangle = true) noexcept
      : addr_(addr), resolver_(resolver), scratch_(scratch, N),
        demangle_(demangle) {}

  [[nodiscard]] constexpr uintptr_t address() const noexcept { return addr_; }
  [[nodiscard]] constexpr symbol_resolver_ref resolver() const noexcept {
    return resolver_;
  }
  [[nodiscard]] constexpr span<char> scratch() const noexcept {
    return scratch_;
  }
  [[nodiscard]] constexpr bool demangle() const noexcept { return demangle_; }

private:
  uintptr_t addr_{0};
  symbol_resolver_ref resolver_{};
  span<char> scratch_{};
  bool demangle_{true};
};

template <> struct formatter<remote_symbol_view> {
  char mode{'\0'}; // '#' = verbose (image!symbol+off), '\0' = standard

  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    if (!spec.empty() && spec.front() == '#') {
      mode = '#';
    }
  }

  void format(const remote_symbol_view &view, const sink &out) const noexcept {
    if (view.address() == 0) {
      out.write("(null)");
      return;
    }

    if (!view.resolver()) {
      microfmt::format_to(out, "{:#x}", view.address());
      return;
    }

    resolved_symbol_info info{};
    bool ok = view.resolver().resolve(view.address(), view.scratch(), info);

    // 1. Unresolved fallback
    if (!ok || (!info.has_symbol() && !info.has_image())) {
      microfmt::format_to(out, "<unknown@{:#x}>", view.address());
      return;
    }

    // Precise or nearest symbol matched
    if (info.has_symbol()) {
      if (mode == '#' && info.has_image()) {
        microfmt::format_to(out, "{}!", info.image_name);
      }

      if (view.demangle()) {
        microfmt::format_to(out, "{}", as_demangled(info.symbol_name));
      } else {
        out.write(info.symbol_name);
      }

      if (info.offset_from_symbol > 0) {
        microfmt::format_to(out, "+{:#x}", info.offset_from_symbol);
      }
      return;
    }

    // Module/Image known, but symbol stripped (e.g. nvgpu.ko+0x1420)
    if (info.has_image()) {
      microfmt::format_to(out, "{}+{:#x}", info.image_name,
                          info.offset_from_image);
    }
  }
};

// Convenience factory
template <size_t N>
[[nodiscard]] constexpr auto
make_remote_symbol(uintptr_t addr, symbol_resolver_ref resolver,
                   char (&scratch)[N], bool demangle = true) noexcept {
  return remote_symbol_view(addr, resolver, scratch, demangle);
}

} // namespace microfmt