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

/**
 * @brief Formats a human-readable description of a faulting address.
 *
 * Produces detailed (symbol/module-aware) output when a resolver is available,
 * and falls back to a raw `<fault:0x..>` marker otherwise.
 *
 * @param out Destination sink.
 * @param addr Faulting address.
 * @param resolver Optional symbol resolver.
 * @param scratch Scratch buffer for symbol strings.
 */
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
          microfmt::format_to(out, MICROFMT_STRING("{}!"), info.image_name);
        }
        microfmt::format_to(out, MICROFMT_STRING("{}"),
                            as_demangled(info.symbol_name));
        if (info.offset_from_symbol > 0) {
          microfmt::format_to(out, MICROFMT_STRING("+{:#x}"),
                              info.offset_from_symbol);
        }
      } else {
        // Module known, symbol stripped: e.g. "faulty_driver.ko+0x4200"
        microfmt::format_to(out, MICROFMT_STRING("{}+{:#x}"), info.image_name,
                            info.offset_from_image);
      }

      out.write(">");
      return;
    }
  }

  // Fallback raw hex address
  microfmt::format_to(out, MICROFMT_STRING("<fault:{:#x}>"), addr);
}

// ============================================================================
// Symbolic Remote Function / Code Pointer View
// ============================================================================

/**
 * @brief Symbolic view of a remote function / code pointer.
 *
 * Renders `(null)`, a raw `0x..` address, or a resolved symbol depending on
 * the format mode and resolver availability.
 */
class remote_fn_ptr {
public:
  /**
   * @brief Constructs an empty (null) view.
   */
  constexpr remote_fn_ptr() noexcept = default;

  /**
   * @brief Constructs a view over a remote code address.
   * @param addr Code address to render.
   * @param resolver Optional symbol resolver.
   * @param scratch Scratch buffer for symbol strings.
   */
  constexpr remote_fn_ptr(uintptr_t addr, symbol_resolver_ref resolver,
                          span<char> scratch) noexcept
      : addr_(addr), resolver_(resolver), scratch_(scratch) {}

  /**
   * @brief Constructs a view over a remote code address with a C-array
   * scratch buffer.
   * @tparam N Scratch buffer size.
   * @param addr Code address to render.
   * @param resolver Optional symbol resolver.
   * @param scratch Scratch buffer for symbol strings.
   */
  template <size_t N>
  constexpr remote_fn_ptr(uintptr_t addr, symbol_resolver_ref resolver,
                          char (&scratch)[N]) noexcept
      : addr_(addr), resolver_(resolver), scratch_(scratch, N) {}

  /**
   * @brief Returns the code address.
   * @return Absolute address, or `0` when null.
   */
  [[nodiscard]] constexpr uintptr_t address() const noexcept { return addr_; }
  /**
   * @brief Reports whether the address is null.
   * @return `true` when the pointer is null.
   */
  [[nodiscard]] constexpr bool is_null() const noexcept { return addr_ == 0; }
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
  /// Code address.
  uintptr_t addr_{0};
  /// Symbol resolver handle.
  symbol_resolver_ref resolver_{};
  /// Scratch span for symbol strings.
  span<char> scratch_{};
};

/**
 * @brief Formatter for @ref remote_fn_ptr.
 *
 * Modes: `x`/`p` force raw hex output; `#` requests verbose `image!symbol+off`
 * output; the default resolves and demangles the symbol.
 */
template <> struct formatter<remote_fn_ptr> {
  /**
   * @brief Output mode selected by the first specifier character.
   */
  char mode{'\0'};

  /**
   * @brief Parses the leading mode character.
   * @param ctx Format parse context exposing the specifier text.
   */
  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    if (!spec.empty()) {
      mode = spec.front();
    }
  }

  /**
   * @brief Renders the remote code pointer.
   * @param fn The code pointer view to format.
   * @param out Destination sink.
   */
  void format(const remote_fn_ptr &fn, const sink &out) const noexcept {
    if (fn.is_null()) {
      out.write("(null)");
      return;
    }

    if (mode == 'x' || mode == 'p' || !fn.resolver()) {
      microfmt::format_to(out, MICROFMT_STRING("{:#x}"), fn.address());
      return;
    }

    auto sym_view =
        remote_symbol_view(fn.address(), fn.resolver(), fn.scratch(), true);
    if (mode == '#') {
      microfmt::format_to(out, MICROFMT_STRING("{:#}"), sym_view);
    } else {
      microfmt::format_to(out, MICROFMT_STRING("{}"), sym_view);
    }
  }
};

// ============================================================================
// Diagnostics-Aware Remote Struct View
// ============================================================================

/**
 * @brief Lazily-loaded remote object reference with diagnostics support.
 *
 * @tparam T Referenced remote object type.
 */
template <typename T> class remote_diag_ref {
public:
  /**
   * @brief Constructs a diagnostics-aware remote object reference.
   * @param addr Absolute address of the object.
   * @param space Address space the object lives in.
   * @param resolver Symbol resolver used on read faults.
   * @param scratch Reusable, aligned object scratch buffer.
   * @param str_scratch Scratch buffer for fault/symbol strings.
   */
  constexpr remote_diag_ref(uintptr_t addr, address_space_ref space,
                            symbol_resolver_ref resolver,
                            span<std::byte> scratch,
                            span<char> str_scratch = {}) noexcept
      : addr_(addr), space_(space), resolver_(resolver), scratch_(scratch),
        str_scratch_(str_scratch) {}

  /**
   * @brief Returns the remote object address.
   * @return Absolute address, or `0` when null.
   */
  [[nodiscard]] constexpr uintptr_t address() const noexcept { return addr_; }
  /**
   * @brief Reports whether the reference points to address zero.
   * @return `true` when the reference is null.
   */
  [[nodiscard]] constexpr bool is_null() const noexcept { return addr_ == 0; }

  /**
   * @brief Loads the object into the scratch buffer.
   * @param out_ptr Receives a pointer into the scratch buffer holding the
   * loaded object.
   * @return `true` on success, `false` otherwise.
   */
  [[nodiscard]] bool load(T *&out_ptr) const noexcept {
    out_ptr = detail::scratch_object<T>(scratch_);
    if (!out_ptr)
      return false;

    return space_.read_bytes(addr_, out_ptr, sizeof(T));
  }

  /**
   * @brief Returns the address space handle.
   * @return Bound @ref address_space_ref.
   */
  [[nodiscard]] constexpr address_space_ref space() const noexcept {
    return space_;
  }
  /**
   * @brief Returns the symbol resolver handle.
   * @return Bound @ref symbol_resolver_ref.
   */
  [[nodiscard]] constexpr symbol_resolver_ref resolver() const noexcept {
    return resolver_;
  }
  /**
   * @brief Returns the string scratch buffer.
   * @return Scratch span used for fault/symbol strings.
   */
  [[nodiscard]] constexpr span<char> str_scratch() const noexcept {
    return str_scratch_;
  }

private:
  /// Remote object address.
  uintptr_t addr_{0};
  /// Address space handle.
  address_space_ref space_{};
  /// Symbol resolver handle.
  symbol_resolver_ref resolver_{};
  /// Object scratch buffer.
  span<std::byte> scratch_{};
  /// String scratch buffer.
  span<char> str_scratch_{};
};

/**
 * @brief Formatter for @ref remote_diag_ref.
 *
 * Renders a detailed fault description when the object cannot be loaded.
 *
 * @tparam T Referenced remote object type.
 */
template <typename T> struct formatter<remote_diag_ref<T>> {
  /**
   * @brief Specifier forwarded to the loaded object's formatter.
   */
  std::string_view spec_{""};

  /**
   * @brief Captures the specifier for the element formatter.
   * @param ctx Format parse context exposing the specifier text.
   */
  constexpr void parse(format_parse_context &ctx) noexcept {
    spec_ = ctx.spec();
  }

  /**
   * @brief Loads and renders the remote object (or a fault description).
   * @param view The diagnostics-aware reference to format.
   * @param out Destination sink.
   */
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
