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

/**
 * @brief Raw resolution result as returned by a resolver backend.
 */
struct raw_resolved_symbol {
  /**
   * @brief Nearest symbol name (may be empty when stripped).
   */
  std::string_view symbol_name{""};
  /**
   * @brief Base address of the matched symbol.
   */
  uintptr_t symbol_base{0};
  /**
   * @brief `true` when the match is exact (not merely nearest).
   */
  bool is_exact{false};

  /**
   * @brief Owning image/module name.
   */
  std::string_view image_name{""};
  /**
   * @brief Load base of the owning image.
   */
  uintptr_t image_load_base{0};
};

/**
 * @brief Derived symbol info with precomputed relative offsets.
 */
struct resolved_symbol_info {
  /**
   * @brief Nearest symbol name (may be empty when stripped).
   */
  std::string_view symbol_name{""};
  /**
   * @brief Base address of the matched symbol.
   */
  uintptr_t symbol_base{0};
  /**
   * @brief Offset of the address from @ref symbol_base.
   */
  uintptr_t offset_from_symbol{0};
  /**
   * @brief `true` when the match is exact (not merely nearest).
   */
  bool is_exact{false};

  /**
   * @brief Owning image/module name.
   */
  std::string_view image_name{""};
  /**
   * @brief Load base of the owning image.
   */
  uintptr_t image_load_base{0};
  /**
   * @brief Offset of the address from @ref image_load_base.
   */
  uintptr_t offset_from_image{0};

  /**
   * @brief Reports whether a symbol name is available.
   * @return `true` when @ref symbol_name is non-empty.
   */
  [[nodiscard]] constexpr bool has_symbol() const noexcept {
    return !symbol_name.empty();
  }

  /**
   * @brief Reports whether an owning image is known.
   * @return `true` when @ref image_name is non-empty.
   */
  [[nodiscard]] constexpr bool has_image() const noexcept {
    return !image_name.empty();
  }
};

// ============================================================================
// Static Customization Point: symbol_resolver_traits
// ============================================================================

/**
 * @brief Static customization point for symbol resolver backends.
 * @tparam Tag Tag identifying the resolver implementation.
 */
template <typename Tag> struct symbol_resolver_traits;

// ============================================================================
// Minimal-Stack Type-Erased Symbol Resolver (2 Words)
// ============================================================================

/**
 * @brief Type-erased, two-word handle to a symbol resolver.
 *
 * Resolves addresses to symbols and derives the relative offsets stored in
 * @ref resolved_symbol_info.
 */
class symbol_resolver_ref {
public:
  /**
   * @brief Virtual table of symbol-resolution operations.
   */
  struct vtable {
    /**
     * @brief Resolves an address. See @ref symbol_resolver_ref::resolve.
     */
    bool (*resolve)(const void *ctx, uintptr_t addr, span<char> scratch,
                    raw_resolved_symbol &out_raw) noexcept;
  };

  /**
   * @brief Constructs an empty (invalid) handle.
   */
  constexpr symbol_resolver_ref() noexcept = default;

  /**
   * @brief Constructs a handle for a stateless resolver tag.
   * @tparam Tag Resolver tag type.
   * @tparam Traits Specialized traits, enabled when `context_type` is `void`.
   */
  template <
      typename Tag, typename Traits = symbol_resolver_traits<Tag>,
      std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  constexpr explicit symbol_resolver_ref(Tag) noexcept
      : ctx_(nullptr), vtbl_(&s_vtbl<Tag>) {}

  /**
   * @brief Constructs a handle for a stateful resolver tag.
   * @tparam Tag Resolver tag type.
   * @tparam Context Concrete context type.
   * @tparam Traits Specialized traits, enabled when `context_type` is non-void
   * and @p Context converts to it.
   * @param ctx Context object performing the resolution.
   */
  template <typename Tag, typename Context,
            typename Traits = symbol_resolver_traits<Tag>,
            std::enable_if_t<
                !std::is_void_v<typename Traits::context_type> &&
                    std::is_convertible_v<
                        const Context *, const typename Traits::context_type *>,
                int> = 0>
  constexpr symbol_resolver_ref(Tag, const Context &ctx) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag>) {}

  /**
   * @brief Creates a handle for a stateless resolver tag.
   * @tparam Tag Resolver tag type.
   * @tparam Traits Specialized traits, enabled when `context_type` is `void`.
   * @return An @ref symbol_resolver_ref for the tag.
   */
  template <
      typename Tag, typename Traits = symbol_resolver_traits<Tag>,
      std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr symbol_resolver_ref make() noexcept {
    return symbol_resolver_ref(Tag{});
  }

  /**
   * @brief Creates a handle for a stateful resolver tag.
   * @tparam Tag Resolver tag type.
   * @tparam Context Concrete context type.
   * @tparam Traits Specialized traits, enabled when `context_type` is non-void.
   * @param ctx Context object performing the resolution.
   * @return An @ref symbol_resolver_ref bound to @p ctx.
   */
  template <
      typename Tag, typename Context,
      typename Traits = symbol_resolver_traits<Tag>,
      std::enable_if_t<!std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr symbol_resolver_ref
  make(const Context &ctx) noexcept {
    return symbol_resolver_ref(Tag{}, ctx);
  }

  /**
   * @brief Resolves an address and computes the derived offsets.
   * @param addr Address to resolve.
   * @param scratch Scratch buffer available to the backend.
   * @param out_info Receives the derived symbol info.
   * @return `true` on success, `false` when the handle is empty or resolution
   * fails.
   */
  [[nodiscard]] bool resolve(uintptr_t addr, span<char> scratch,
                             resolved_symbol_info &out_info) const noexcept {
    raw_resolved_symbol raw{};
    return resolve(addr, scratch, raw, out_info);
  }

  /**
   * @brief Resolves an address using caller-owned intermediate storage.
   * @param addr Address to resolve.
   * @param scratch Scratch buffer available to the backend.
   * @param raw_storage Storage used for the backend's raw result.
   * @param out_info Receives the derived symbol info.
   * @return `true` on success, `false` when the handle is empty or resolution
   * fails.
   */
  [[nodiscard]] bool
  resolve(uintptr_t addr, span<char> scratch,
          raw_resolved_symbol &raw_storage,
          resolved_symbol_info &out_info) const noexcept {
    if (!vtbl_)
      return false;

    raw_storage = {};
    if (!vtbl_->resolve(ctx_, addr, scratch, raw_storage)) {
      return false;
    }

    out_info.symbol_name = raw_storage.symbol_name;
    out_info.symbol_base = raw_storage.symbol_base;
    out_info.is_exact = raw_storage.is_exact;
    out_info.image_name = raw_storage.image_name;
    out_info.image_load_base = raw_storage.image_load_base;

    // Derive relative offsets
    if (raw_storage.symbol_base != 0 && addr >= raw_storage.symbol_base) {
      out_info.offset_from_symbol = addr - raw_storage.symbol_base;
    } else {
      out_info.offset_from_symbol = 0;
    }

    if (raw_storage.image_load_base != 0 &&
        addr >= raw_storage.image_load_base) {
      out_info.offset_from_image = addr - raw_storage.image_load_base;
    } else {
      out_info.offset_from_image = 0;
    }

    return true;
  }

  /**
   * @brief Reports whether the handle is bound to a resolver.
   * @return `true` when the handle is valid.
   */
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

/**
 * @brief Formattable view resolving and rendering a remote code address.
 */
class remote_symbol_view {
public:
  /**
   * @brief Constructs an empty (null) view.
   */
  constexpr remote_symbol_view() noexcept = default;

  /**
   * @brief Constructs a view over a remote address.
   * @param addr Address to resolve and render.
   * @param resolver Symbol resolver to use.
   * @param scratch Scratch buffer for symbol-name strings.
   * @param demangle When `true`, demangle Itanium symbol names.
   */
  constexpr remote_symbol_view(uintptr_t addr, symbol_resolver_ref resolver,
                               span<char> scratch,
                               bool demangle = true) noexcept
      : addr_(addr), resolver_(resolver), scratch_(scratch),
        demangle_(demangle) {}

  /**
   * @brief Constructs a view over a remote address with a fixed C-array
   * scratch buffer.
   * @tparam N Scratch buffer size.
   * @param addr Address to resolve and render.
   * @param resolver Symbol resolver to use.
   * @param scratch Scratch buffer for symbol-name strings.
   * @param demangle When `true`, demangle Itanium symbol names.
   */
  template <size_t N>
  constexpr remote_symbol_view(uintptr_t addr, symbol_resolver_ref resolver,
                               char (&scratch)[N],
                               bool demangle = true) noexcept
      : addr_(addr), resolver_(resolver), scratch_(scratch, N),
        demangle_(demangle) {}

  /**
   * @brief Returns the address being resolved.
   * @return Absolute address, or `0` when null.
   */
  [[nodiscard]] constexpr uintptr_t address() const noexcept { return addr_; }
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
   * @brief Reports whether symbol names are demangled.
   * @return `true` when demangling is enabled.
   */
  [[nodiscard]] constexpr bool demangle() const noexcept { return demangle_; }

private:
  /// Address to resolve.
  uintptr_t addr_{0};
  /// Symbol resolver handle.
  symbol_resolver_ref resolver_{};
  /// Scratch span for symbol strings.
  span<char> scratch_{};
  /// Whether to demangle names.
  bool demangle_{true};
};

/**
 * @brief Formatter for @ref remote_symbol_view.
 *
 * Supports a leading `#` for verbose output (`image!symbol+off`); otherwise
 * emits `symbol+off`, `image+off`, or `<unknown@0x..>`/`(null)`.
 */
template <> struct formatter<remote_symbol_view> {
  /**
   * @brief Output mode: `'#'` = verbose `image!symbol+off`, `'\0'` = standard.
   */
  char mode{'\0'}; // '#' = verbose (image!symbol+off), '\0' = standard

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
   * @brief Resolves and renders the remote address.
   * @param view The symbol view to format.
   * @param out Destination sink.
   */
  void format(const remote_symbol_view &view, const sink &out) const noexcept {
    if (view.address() == 0) {
      out.write("(null)");
      return;
    }

    if (!view.resolver()) {
      microfmt::format_to(out, MICROFMT_STRING("{:#x}"), view.address());
      return;
    }

    resolved_symbol_info info{};
    bool ok = view.resolver().resolve(view.address(), view.scratch(), info);

    // 1. Unresolved fallback
    if (!ok || (!info.has_symbol() && !info.has_image())) {
      microfmt::format_to(out, MICROFMT_STRING("<unknown@{:#x}>"),
                          view.address());
      return;
    }

    // Precise or nearest symbol matched
    if (info.has_symbol()) {
      if (mode == '#' && info.has_image()) {
        microfmt::format_to(out, MICROFMT_STRING("{}!"), info.image_name);
      }

      if (view.demangle()) {
        microfmt::format_to(out, MICROFMT_STRING("{}"),
                            as_demangled(info.symbol_name));
      } else {
        out.write(info.symbol_name);
      }

      if (info.offset_from_symbol > 0) {
        microfmt::format_to(out, MICROFMT_STRING("+{:#x}"),
                            info.offset_from_symbol);
      }
      return;
    }

    // Module/Image known, but symbol stripped (e.g. nvgpu.ko+0x1420)
    if (info.has_image()) {
      microfmt::format_to(out, MICROFMT_STRING("{}+{:#x}"), info.image_name,
                          info.offset_from_image);
    }
  }
};

/**
 * @brief Convenience factory building a @ref remote_symbol_view.
 * @tparam N Scratch buffer size.
 * @param addr Address to resolve and render.
 * @param resolver Symbol resolver to use.
 * @param scratch Scratch buffer for symbol-name strings.
 * @param demangle When `true`, demangle Itanium symbol names.
 * @return A configured @ref remote_symbol_view.
 */
template <size_t N>
[[nodiscard]] constexpr auto
make_remote_symbol(uintptr_t addr, symbol_resolver_ref resolver,
                   char (&scratch)[N], bool demangle = true) noexcept {
  return remote_symbol_view(addr, resolver, scratch, demangle);
}

} // namespace microfmt