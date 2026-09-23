// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file uuid.hpp @brief UUID formatting views and optional Boost.UUID support. */

#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <reloco/type_id.hpp>
#include <string_view>
#include <type_traits>

namespace microfmt {

// ============================================================================
// UUID View Definition
// ============================================================================

struct MICROFMT_API_CLASS uuid_view {
  span<const uint8_t> bytes{}; // 16 bytes
  bool uppercase{false};
  bool braced{false}; // Wrap in { ... }
};

// ============================================================================
// Core Formatter Logic
// ============================================================================

namespace detail {
MICROFMT_API void format_uuid_bytes(const sink &out, span<const uint8_t> bytes, bool is_upper,
                                     bool is_braced) noexcept;

#if MICROFMT_SHARED_PROVIDE_DEFINITIONS
#include "uuid.ipp"
#endif

template <typename T, typename = void> struct is_uuid_container : std::false_type {};

template <typename T>
struct is_uuid_container<
    T, std::void_t<decltype(std::declval<const T &>().data()), decltype(std::declval<const T &>().size())>>
    : std::integral_constant<bool, std::is_convertible_v<decltype(std::declval<const T &>().data()), const uint8_t *> &&
                                       std::is_convertible_v<decltype(std::declval<const T &>().size()), size_t>> {};
} // namespace detail

// ============================================================================
// Factory Helpers (All Overloads)
// ============================================================================

// From raw byte span / pointer + size
[[nodiscard]] constexpr uuid_view uuid(span<const uint8_t> b, bool uppercase = false, bool braced = false) noexcept {
  return uuid_view{b, uppercase, braced};
}

// From raw 16-byte C-array
[[nodiscard]] constexpr uuid_view uuid(const uint8_t (&arr)[16], bool uppercase = false, bool braced = false) noexcept {
  return uuid_view{span<const uint8_t>(arr, 16), uppercase, braced};
}

// From 16-byte std::array or contiguous container
template <typename ArrayT, std::enable_if_t<detail::is_uuid_container<ArrayT>::value, int> = 0>
[[nodiscard]] constexpr uuid_view uuid(const ArrayT &arr, bool uppercase = false, bool braced = false) noexcept {
  return uuid_view{span<const uint8_t>(arr.data(), arr.size()), uppercase, braced};
}

template <typename ArrayT, std::enable_if_t<!std::is_lvalue_reference_v<ArrayT> &&
                                                detail::is_uuid_container<std::remove_reference_t<ArrayT>>::value,
                                            int> = 0>
[[nodiscard]] constexpr uuid_view uuid(ArrayT &&, bool = false, bool = false) noexcept = delete;

#if MICROFMT_HAS_BOOST_UUID
// From boost::uuids::uuid
[[nodiscard]] constexpr uuid_view uuid(const boost::uuids::uuid &u, bool uppercase = false,
                                       bool braced = false) noexcept {
  return uuid_view{span<const uint8_t>(u.data, 16), uppercase, braced};
}

[[nodiscard]] constexpr uuid_view uuid(boost::uuids::uuid &&, bool = false, bool = false) noexcept = delete;
#endif

// ============================================================================
// Formatter Specializations
// ============================================================================

template <> struct formatter<uuid_view> {
  bool uppercase{false};
  bool braced{false};

  constexpr void parse(format_parse_context &ctx) noexcept {
    for (char c : ctx.spec()) {
      if (c == 'X')
        uppercase = true;
      if (c == '#')
        braced = true;
    }
  }

  void format(const uuid_view &u, const sink &out) const noexcept {
    detail::format_uuid_bytes(out, u.bytes, u.uppercase || uppercase, u.braced || braced);
  }
};

#if MICROFMT_HAS_BOOST_UUID
template <> struct formatter<boost::uuids::uuid> {
  bool uppercase{false};
  bool braced{false};

  constexpr void parse(format_parse_context &ctx) noexcept {
    for (char c : ctx.spec()) {
      if (c == 'X')
        uppercase = true;
      if (c == '#')
        braced = true;
    }
  }

  void format(const boost::uuids::uuid &u, const sink &out) const noexcept {
    detail::format_uuid_bytes(out, span<const uint8_t>(u.data, 16), uppercase, braced);
  }
};
#endif

} // namespace microfmt

// See <reloco/type_id.hpp> for the full RELOCO_TYPE_ID_NAME rationale;
// defined here, alongside uuid_view's own definition.
RELOCO_TYPE_ID_NAME(microfmt::uuid_view, "microfmt::uuid_view");