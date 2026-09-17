// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file remote_layout_accessor.hpp
 * @brief Reusable callback and typed-offset accessors for remote layouts. */

#include "address_space.hpp"

#include <cstddef>
#include <limits>
#include <type_traits>

namespace microfmt {

namespace detail {

template <typename To, typename From>
[[nodiscard]] constexpr bool convert_remote_layout_value(From value,
                                                         To &out) noexcept {
  if constexpr (std::is_same<To, From>::value) {
    out = value;
    return true;
  } else {
    static_assert(std::is_integral<To>::value &&
                      std::is_unsigned<To>::value,
                  "converted layout values must be unsigned integers");
    static_assert(std::is_integral<From>::value &&
                      std::is_unsigned<From>::value,
                  "remote layout storage must be an unsigned integer");

    if constexpr (sizeof(From) > sizeof(To)) {
      if (value > static_cast<From>(std::numeric_limits<To>::max()))
        return false;
    }
    out = static_cast<To>(value);
    return true;
  }
}

} // namespace detail

/**
 * @brief Describes a property stored at a fixed offset in a remote object.
 * @tparam Stored Target-side storage type.
 */
template <typename Stored> struct remote_layout_offset {
  ptrdiff_t offset;
};

/**
 * @brief Reads a remote layout property through a retained callback.
 *
 * The callback is stored by value and receives the address space, object
 * address, output reference, and any query-specific inputs.
 *
 * @tparam Value Public value produced by the accessor.
 * @tparam Callback Retained callback type.
 */
template <typename Value, typename Callback>
class remote_layout_accessor {
public:
  using value_type = Value;

  static_assert(std::is_nothrow_copy_constructible<Callback>::value,
                "remote layout callbacks must be nothrow copy-constructible");

  constexpr explicit remote_layout_accessor(Callback callback) noexcept
      : callback_(callback) {}

  template <typename... Inputs>
  [[nodiscard]] bool read(address_space_ref space, uintptr_t object_address,
                          Value &out, Inputs... inputs) const noexcept {
    static_assert(
        std::is_nothrow_invocable_r<bool, const Callback &, address_space_ref,
                                    uintptr_t, Value &, Inputs...>::value,
        "remote layout callback must be noexcept and return bool");
    return callback_(space, object_address, out, inputs...);
  }

private:
  Callback callback_;
};

/**
 * @brief Reads a remote layout property from a typed fixed offset.
 */
template <typename Value, typename Stored>
class remote_layout_accessor<Value, remote_layout_offset<Stored>> {
public:
  using value_type = Value;

  constexpr explicit remote_layout_accessor(
      remote_layout_offset<Stored> source) noexcept
      : offset_(source.offset) {}

  template <typename... Inputs>
  [[nodiscard]] bool read(address_space_ref space, uintptr_t object_address,
                          Value &out, Inputs...) const noexcept {
    Stored stored{};
    if (!space.read(detail::add_address_offset(object_address, offset_),
                    stored))
      return false;
    return detail::convert_remote_layout_value(stored, out);
  }

private:
  ptrdiff_t offset_;
};

/**
 * @brief Uniform query wrapper around callback and typed-offset accessors.
 */
template <typename Value, typename Source> class remote_layout_query {
public:
  constexpr explicit remote_layout_query(Source source) noexcept
      : accessor_(source) {}

  template <typename... Inputs>
  [[nodiscard]] bool operator()(address_space_ref space,
                                uintptr_t object_address, Value &out,
                                Inputs... inputs) const noexcept {
    return accessor_.read(space, object_address, out, inputs...);
  }

private:
  remote_layout_accessor<Value, Source> accessor_;
};

/**
 * @brief Creates a query backed by a callback retained by value.
 */
template <typename Value, typename Callback>
[[nodiscard]] constexpr auto
make_remote_layout_query(Callback callback) noexcept {
  return remote_layout_query<Value, Callback>(callback);
}

/**
 * @brief Creates a query backed by a typed remote-object offset.
 */
template <typename Value, typename Stored = Value>
[[nodiscard]] constexpr auto
make_remote_offset_query(ptrdiff_t offset) noexcept {
  using source_type = remote_layout_offset<Stored>;
  return remote_layout_query<Value, source_type>(source_type{offset});
}

} // namespace microfmt
