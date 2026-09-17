// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file variant.hpp @brief `std::variant` and non-owning variant view
 * formatting. */

#include "../microfmt.hpp"
#include <string_view>
#include <type_traits>
#include <variant>

namespace microfmt {

// -----------------------------------------------------------------------------
// Non-Owning Variant View Wrapper
// -----------------------------------------------------------------------------
/**
 * @brief Non-owning view over a `std::variant` with optional decorations.
 *
 * @tparam Ts The alternative types of the wrapped variant.
 */
template <typename... Ts> class MICROFMT_POINTER variant_view {
public:
  /**
   * @brief The wrapped `std::variant` type.
   */
  using variant_type = std::variant<Ts...>;

  /**
   * @brief Constructs the view.
   * @param var Referenced variant.
   * @param prefix Text emitted before the active alternative.
   * @param suffix Text emitted after the active alternative.
   * @param show_index When `true`, include the alternative index.
   */
  constexpr explicit variant_view(
                                  const variant_type &var
                                      MICROFMT_LIFETIMEBOUND,
                                  microfmt::string_view prefix = "",
                                  microfmt::string_view suffix = "",
                                  bool show_index = false) noexcept
      : var_(var), prefix_(prefix), suffix_(suffix), show_index_(show_index) {}

  constexpr explicit variant_view(variant_type &&,
                                  microfmt::string_view = "",
                                  microfmt::string_view = "",
                                  bool = false) = delete;

  constexpr explicit variant_view(const variant_type &&,
                                  microfmt::string_view = "",
                                  microfmt::string_view = "",
                                  bool = false) = delete;

  /**
   * @brief Returns the referenced variant.
   * @return The wrapped `std::variant`.
   */
  [[nodiscard]] constexpr const variant_type &get() const noexcept {
    return *var_;
  }
  /**
   * @brief Returns the prefix decoration.
   * @return Text emitted before the active alternative.
   */
  [[nodiscard]] constexpr microfmt::string_view prefix() const noexcept {
    return prefix_;
  }
  /**
   * @brief Returns the suffix decoration.
   * @return Text emitted after the active alternative.
   */
  [[nodiscard]] constexpr microfmt::string_view suffix() const noexcept {
    return suffix_;
  }
  /**
   * @brief Reports whether the alternative index is displayed.
   * @return `true` when the index is included in output.
   */
  [[nodiscard]] constexpr bool show_index() const noexcept {
    return show_index_;
  }

private:
  /// Referenced variant.
  value_ref<const variant_type> var_;
  /// Prefix decoration.
  microfmt::string_view prefix_;
  /// Suffix decoration.
  microfmt::string_view suffix_;
  /// Whether to include the active alternative index.
  bool show_index_;
};

// -----------------------------------------------------------------------------
// View Factory Helpers (microfmt::as_variant)
// -----------------------------------------------------------------------------
/**
 * @brief Wraps a variant for plain formatting.
 * @tparam Ts Alternative types of @p var.
 * @param var Variant to format.
 * @return A bare @ref variant_view.
 */
template <typename... Ts>
[[nodiscard]] constexpr auto
as_variant(const std::variant<Ts...> &var MICROFMT_LIFETIMEBOUND) noexcept {
  return variant_view<Ts...>(var, "", "", false);
}

template <typename... Ts>
[[nodiscard]] constexpr auto as_variant(std::variant<Ts...> &&) noexcept =
    delete;

template <typename... Ts>
[[nodiscard]] constexpr auto
as_variant(const std::variant<Ts...> &&) noexcept = delete;

/**
 * @brief Wraps a variant with custom prefix/suffix decorations.
 * @tparam Ts Alternative types of @p var.
 * @param var Variant to format.
 * @param prefix Text emitted before the active alternative.
 * @param suffix Text emitted after the active alternative.
 * @return A decorated @ref variant_view.
 */
template <typename... Ts>
[[nodiscard]] constexpr auto
as_variant(const std::variant<Ts...> &var MICROFMT_LIFETIMEBOUND,
           microfmt::string_view prefix,
           microfmt::string_view suffix) noexcept {
  return variant_view<Ts...>(var, prefix, suffix, false);
}

template <typename... Ts>
[[nodiscard]] constexpr auto
as_variant(std::variant<Ts...> &&, microfmt::string_view,
           microfmt::string_view) noexcept = delete;

template <typename... Ts>
[[nodiscard]] constexpr auto
as_variant(const std::variant<Ts...> &&, microfmt::string_view,
           microfmt::string_view) noexcept = delete;

/**
 * @brief Wraps a variant for debug output (`variant(index, ...)`).
 * @tparam Ts Alternative types of @p var.
 * @param var Variant to format.
 * @param show_index When `true`, include the alternative index.
 * @return A debug-oriented @ref variant_view.
 */
template <typename... Ts>
[[nodiscard]] constexpr auto
as_debug_variant(
                 const std::variant<Ts...> &var MICROFMT_LIFETIMEBOUND,
                 bool show_index = false) noexcept {
  return variant_view<Ts...>(var, "variant", "", show_index);
}

template <typename... Ts>
[[nodiscard]] constexpr auto
as_debug_variant(std::variant<Ts...> &&, bool = false) noexcept = delete;

template <typename... Ts>
[[nodiscard]] constexpr auto
as_debug_variant(const std::variant<Ts...> &&,
                 bool = false) noexcept = delete;

// -----------------------------------------------------------------------------
// Formatter for variant_view targeting const microfmt::sink&
// -----------------------------------------------------------------------------
/**
 * @brief Formatter for @ref variant_view.
 *
 * Supports a leading `?` (debug), `i` (index), or `#` (index) mode character;
 * the rest of the specifier is forwarded to the active alternative's formatter.
 *
 * @tparam Ts Alternative types of the wrapped variant.
 */
template <typename... Ts> struct formatter<variant_view<Ts...>> {
  /**
   * @brief Leading mode character (`?`, `i`, `#`, or `'\0'`).
   */
  char spec_mode{'\0'};
  /**
   * @brief Element specifier forwarded to the active alternative.
   */
  microfmt::string_view elem_spec{""};

  /**
   * @brief Parses the leading mode and the element specifier.
   * @param ctx Format parse context exposing the specifier text.
   */
  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    if (!spec.empty() &&
        (spec.front() == '?' || spec.front() == 'i' || spec.front() == '#')) {
      spec_mode = spec.front();
      spec.remove_prefix(1);
    }
    elem_spec = spec;
  }

  /**
   * @brief Renders the active alternative with the configured decorations.
   * @param view The variant view to format.
   * @param out Destination sink.
   */
  void format(const variant_view<Ts...> &view, const sink &out) const noexcept {
    const auto &var = view.get();

    if (var.valueless_by_exception()) {
      out.write("valueless");
      return;
    }

    bool print_parens = false;
    if (spec_mode == '?' || (view.prefix() == "variant" && !view.show_index() &&
                             spec_mode != 'i')) {
      out.write("variant(");
      print_parens = true;
    } else if (spec_mode == 'i' || spec_mode == '#' || view.show_index()) {
      microfmt::format_to(out, MICROFMT_STRING("variant[{}]"), var.index());
      out.write("(");
      print_parens = true;
    } else if (!view.prefix().empty()) {
      out.write(view.prefix());
    }

    std::visit(
        [this, &out](const auto &val) {
          using ValueType = std::decay_t<decltype(val)>;
          formatter<ValueType> element_fmt;
          format_parse_context elem_ctx(elem_spec);
          element_fmt.parse(elem_ctx);
          element_fmt.format(val, out);
        },
        var);

    if (print_parens) {
      out.write(")");
    } else if (!view.suffix().empty()) {
      out.write(view.suffix());
    }
  }
};

// -----------------------------------------------------------------------------
// Formatter for std::variant<Ts...>
// -----------------------------------------------------------------------------
/**
 * @brief Formatter for `std::variant`, delegating to the @ref variant_view
 * formatter.
 * @tparam Ts Alternative types of the variant.
 */
template <typename... Ts> struct formatter<std::variant<Ts...>> {
  /**
   * @brief Internal view formatter used to render alternatives.
   */
  formatter<variant_view<Ts...>> view_formatter_{};

  /**
   * @brief Parses the specifier for the wrapped view formatter.
   * @param ctx Format parse context exposing the specifier text.
   */
  constexpr void parse(format_parse_context &ctx) noexcept {
    view_formatter_.parse(ctx);
  }

  /**
   * @brief Renders the variant's active alternative.
   * @param var Variant to format.
   * @param out Destination sink.
   */
  void format(const std::variant<Ts...> &var, const sink &out) const noexcept {
    view_formatter_.format(as_variant(var), out);
  }
};

/**
 * @brief Formatter for `std::monostate`, rendering it as `null`.
 */
template <> struct formatter<std::monostate> {
  /**
   * @brief No-op parse; monostate accepts no format specifier.
   */
  constexpr void parse(format_parse_context &) noexcept {}

  /**
   * @brief Writes the literal `null`.
   * @param out Destination sink.
   */
  void format(std::monostate, const sink &out) const noexcept {
    out.write("null");
  }
};

} // namespace microfmt