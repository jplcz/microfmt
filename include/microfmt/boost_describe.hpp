#pragma once

#include "microfmt.hpp"
#include <boost/describe.hpp>
#include <type_traits>

namespace microfmt {

namespace detail {

// Check if T has Boost.Describe members (only takes T)
template <typename T>
concept DescribedStruct = boost::describe::has_describe_members<T>::value;

// Check if E is an enum registered with Boost.Describe
template <typename E>
concept DescribedEnum =
    std::is_enum_v<E> && boost::describe::has_describe_enumerators<E>::value;

} // namespace detail

// ============================================================================
// Formatter for Boost.Describe Reflected Enums
// ============================================================================
template <detail::DescribedEnum E> struct formatter<E> {
  constexpr void parse(format_parse_context &ctx) noexcept { (void)ctx; }

  void format(E val, const sink &out) const noexcept {
    using Enumerators = boost::describe::describe_enumerators<E>;
    bool found = false;

    boost::mp11::mp_for_each<Enumerators>([&](auto D) noexcept {
      if (!found && D.value == val) {
        out.write(D.name);
        found = true;
      }
    });

    if (!found) {
      // Fallback: format raw underlying integer if value is unmapped/invalid
      using Underlying = std::underlying_type_t<E>;
      format_to(out, "static_cast<{}>({})",
                sizeof(Underlying) == 1   ? "uint8_t"
                : sizeof(Underlying) == 2 ? "uint16_t"
                                          : "uint32_t",
                static_cast<Underlying>(val));
    }
  }
};

// ============================================================================
// Formatter for Boost.Describe Reflected Structs & Classes
// ============================================================================
template <detail::DescribedStruct T> struct formatter<T> {
  constexpr void parse(format_parse_context &ctx) noexcept { (void)ctx; }

  void format(const T &val, const sink &out) const noexcept {
    using Members =
        boost::describe::describe_members<T, boost::describe::mod_public>;

    out.put('{');
    bool first = true;

    boost::mp11::mp_for_each<Members>([&](auto D) noexcept {
      if (!first) {
        out.write(", ");
      }
      first = false;

      // Output member name
      out.write(D.name);
      out.write(": ");

      // Format member value recursively via microfmt
      format_to(out, "{}", val.*D.pointer);
    });

    out.put('}');
  }
};

} // namespace microfmt
