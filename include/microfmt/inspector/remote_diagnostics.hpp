// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

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
 * @param context Caller-owned symbol-resolution temporaries.
 */
inline void format_remote_fault(const sink &out, uintptr_t addr,
                                symbol_resolver_ref resolver,
                                symbol_resolution_context &context) noexcept {
  if (addr == 0) {
    out.write("<fault:nullptr>");
    return;
  }

  if (resolver) {
    context.raw = {};
    context.resolved = {};
    if (resolver.resolve(addr, context) &&
        ((context.resolved.has_symbol()) ||
         context.resolved.has_image())) {
      const auto &info = context.resolved;
      out.write("<fault@");

      if (info.has_symbol()) {
        if (info.has_image()) {
          out.write(info.image_name);
          out.put('!');
        }
        microfmt::format_to(out, "{}",
                            as_demangled(info.symbol_name));
        if (info.offset_from_symbol > 0) {
          out.write("+0x");
          detail::format_unsigned(
              out, static_cast<uint64_t>(info.offset_from_symbol), 16, false,
              0);
        }
      } else {
        // Module known, symbol stripped: e.g. "faulty_driver.ko+0x4200"
        out.write(info.image_name);
        out.write("+0x");
        detail::format_unsigned(
            out, static_cast<uint64_t>(info.offset_from_image), 16, false, 0);
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

/**
 * @brief Symbolic view of a remote function / code pointer.
 *
 * Renders `(null)`, a raw `0x..` address, or a resolved symbol depending on
 * the format mode and resolver availability.
 */
class RELOCO_POINTER remote_fn_ptr {
public:
  /**
   * @brief Constructs an empty (null) view.
   */
  constexpr remote_fn_ptr() noexcept = default;

  /**
   * @brief Constructs a view over a remote code address.
   * @param addr Code address to render.
   * @param resolver Optional symbol resolver.
   * @param context Caller-owned symbol-resolution temporaries.
   */
  constexpr remote_fn_ptr(uintptr_t addr, symbol_resolver_ref resolver,
                          symbol_resolution_context &context
                              RELOCO_LIFETIMEBOUND) noexcept
      : addr_(addr), resolver_(resolver), context_(&context) {}

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
   * @brief Returns caller-owned symbol-resolution temporaries.
   */
  [[nodiscard]] constexpr symbol_resolution_context &
  context() const noexcept RELOCO_LIFETIMEBOUND {
    return *context_;
  }

private:
  /// Code address.
  uintptr_t addr_{0};
  /// Symbol resolver handle.
  symbol_resolver_ref resolver_{};
  /// Caller-owned symbol-resolution temporaries.
  value_ptr<symbol_resolution_context> context_{};
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
      microfmt::format_to(out, "{:#x}", fn.address());
      return;
    }

    auto sym_view =
        remote_symbol_view(fn.address(), fn.resolver(), fn.context(), true);
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

/**
 * @brief Lazily-loaded remote object reference with diagnostics support.
 *
 * @tparam T Referenced remote object type.
 */
template <typename T> class RELOCO_POINTER remote_diag_ref {
public:
  /**
   * @brief Constructs a diagnostics-aware remote object reference.
   * @param addr Absolute address of the object.
   * @param space Address space the object lives in.
   * @param resolver Symbol resolver used on read faults.
   * @param scratch Reusable, aligned object scratch buffer.
   * @param symbol_context Caller-owned symbol-resolution temporaries.
   */
  constexpr remote_diag_ref(uintptr_t addr, address_space_ref space,
                            symbol_resolver_ref resolver,
                            span<std::byte> scratch RELOCO_LIFETIMEBOUND,
                            symbol_resolution_context &symbol_context
                                RELOCO_LIFETIMEBOUND) noexcept
      : addr_(addr), space_(space), resolver_(resolver), scratch_(scratch),
        symbol_context_(&symbol_context) {}

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
   * @return A pointer into the scratch buffer, or a precise loading error.
   */
  [[nodiscard]] expected<T *, remote_load_error>
  load() const noexcept RELOCO_LIFETIMEBOUND {
    if (addr_ == 0)
      return unexpected(remote_load_error::null_address);
    if (scratch_.size() < sizeof(T))
      return unexpected(remote_load_error::scratch_too_small);
    if (reinterpret_cast<uintptr_t>(scratch_.data()) % alignof(T) != 0)
      return unexpected(remote_load_error::scratch_misaligned);
    if (!space_)
      return unexpected(remote_load_error::invalid_address_space);

    auto *out_ptr = static_cast<T *>(static_cast<void *>(scratch_.data()));
    if (!space_.read_bytes(addr_, out_ptr, sizeof(T)))
      return unexpected(remote_load_error::read_failed);
    return out_ptr;
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
  [[nodiscard]] constexpr symbol_resolution_context &
  symbol_context() const noexcept RELOCO_LIFETIMEBOUND {
    return *symbol_context_;
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
  /// Caller-owned symbol-resolution temporaries.
  value_ptr<symbol_resolution_context> symbol_context_{};
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
  microfmt::string_view spec_{""};

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

    auto staged = view.load();
    if (!staged) {
      format_remote_fault(out, view.address(), view.resolver(),
                          view.symbol_context());
      return;
    }

    formatter<T> elem_fmt;
    format_parse_context pctx(spec_);
    elem_fmt.parse(pctx);
    elem_fmt.format(**staged, out);
  }
};

} // namespace microfmt
