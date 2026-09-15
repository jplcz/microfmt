// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

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
template <typename... Ts> class variant_view {
public:
  using variant_type = std::variant<Ts...>;

  constexpr explicit variant_view(const variant_type &var,
                                  std::string_view prefix = "",
                                  std::string_view suffix = "",
                                  bool show_index = false) noexcept
      : var_(var), prefix_(prefix), suffix_(suffix), show_index_(show_index) {}

  [[nodiscard]] constexpr const variant_type &get() const noexcept {
    return var_;
  }
  [[nodiscard]] constexpr std::string_view prefix() const noexcept {
    return prefix_;
  }
  [[nodiscard]] constexpr std::string_view suffix() const noexcept {
    return suffix_;
  }
  [[nodiscard]] constexpr bool show_index() const noexcept {
    return show_index_;
  }

private:
  const variant_type &var_;
  std::string_view prefix_;
  std::string_view suffix_;
  bool show_index_;
};

// -----------------------------------------------------------------------------
// View Factory Helpers (microfmt::as_variant)
// -----------------------------------------------------------------------------
template <typename... Ts>
[[nodiscard]] constexpr auto
as_variant(const std::variant<Ts...> &var) noexcept {
  return variant_view<Ts...>(var, "", "", false);
}

template <typename... Ts>
[[nodiscard]] constexpr auto as_variant(const std::variant<Ts...> &var,
                                        std::string_view prefix,
                                        std::string_view suffix) noexcept {
  return variant_view<Ts...>(var, prefix, suffix, false);
}

template <typename... Ts>
[[nodiscard]] constexpr auto
as_debug_variant(const std::variant<Ts...> &var,
                 bool show_index = false) noexcept {
  return variant_view<Ts...>(var, "variant", "", show_index);
}

// -----------------------------------------------------------------------------
// Formatter for variant_view targeting const microfmt::sink&
// -----------------------------------------------------------------------------
template <typename... Ts> struct formatter<variant_view<Ts...>> {
  char spec_mode{'\0'};
  std::string_view elem_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept {
    auto spec = ctx.spec();
    if (!spec.empty() &&
        (spec.front() == '?' || spec.front() == 'i' || spec.front() == '#')) {
      spec_mode = spec.front();
      spec.remove_prefix(1);
    }
    elem_spec = spec;
  }

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
      microfmt::format_to(out, "variant[{}]", var.index());
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
template <typename... Ts> struct formatter<std::variant<Ts...>> {
  formatter<variant_view<Ts...>> view_formatter_{};

  constexpr void parse(format_parse_context &ctx) noexcept {
    view_formatter_.parse(ctx);
  }

  void format(const std::variant<Ts...> &var, const sink &out) const noexcept {
    view_formatter_.format(as_variant(var), out);
  }
};

// -----------------------------------------------------------------------------
// Formatter for std::monostate
// -----------------------------------------------------------------------------
template <> struct formatter<std::monostate> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(std::monostate, const sink &out) const noexcept {
    out.write("null");
  }
};

} // namespace microfmt