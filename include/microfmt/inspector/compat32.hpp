// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file compat32.hpp @brief 32-bit compatibility pointers and remote view
 * helpers for 64-bit hosts. */

#include "address_space.hpp"
#include <cstdint>
#include <type_traits>

namespace microfmt {

// ============================================================================
// 32-bit Compatibility Pointer Primitive
// ============================================================================

/**
 * @brief Value-type pointer storing a 32-bit address (for 64-bit hosts).
 * @tparam T Referenced object type.
 */
template <typename T> class alignas(uint32_t) compat32_ptr {
public:
  /// Referenced element type.
  using element_type = T;

  /// Constructs a null pointer.
  constexpr compat32_ptr() noexcept : raw_addr_(0) {}
  /// Constructs a null pointer.
  constexpr compat32_ptr(std::nullptr_t) noexcept : raw_addr_(0) {}
  /// Constructs a pointer from a 32-bit address.
  constexpr explicit compat32_ptr(uint32_t addr) noexcept : raw_addr_(addr) {}

  /**
   * @brief Returns the raw 32-bit address.
   * @return Stored address value.
   */
  [[nodiscard]] constexpr uint32_t raw_value() const noexcept {
    return raw_addr_;
  }
  /**
   * @brief Widens the address to a host pointer-size integer.
   * @return Stored address as @c uintptr_t.
   */
  [[nodiscard]] constexpr uintptr_t to_uintptr() const noexcept {
    return static_cast<uintptr_t>(raw_addr_);
  }
  /**
   * @brief Reports whether the pointer is null.
   * @return `true` when the address is zero.
   */
  [[nodiscard]] constexpr bool is_null() const noexcept {
    return raw_addr_ == 0;
  }
  /**
   * @brief Reports whether the pointer is non-null.
   * @return `true` when the address is non-zero.
   */
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return raw_addr_ != 0;
  }

  /// Compares two pointers for equality.
  constexpr bool operator==(const compat32_ptr &other) const noexcept {
    return raw_addr_ == other.raw_addr_;
  }
  /// Compares two pointers for inequality.
  constexpr bool operator!=(const compat32_ptr &other) const noexcept {
    return raw_addr_ != other.raw_addr_;
  }
  /// Compares against null.
  constexpr bool operator==(std::nullptr_t) const noexcept {
    return raw_addr_ == 0;
  }
  /// Compares against null.
  constexpr bool operator!=(std::nullptr_t) const noexcept {
    return raw_addr_ != 0;
  }

private:
  /// Raw 32-bit address.
  uint32_t raw_addr_{0};
};

static_assert(sizeof(compat32_ptr<void>) == 4,
              "compat32_ptr must be exactly 4 bytes");
static_assert(alignof(compat32_ptr<void>) == 4,
              "compat32_ptr must be 4-byte aligned");

// ============================================================================
// Formatter for compat32_ptr<T>
// ============================================================================

/**
 * @brief Formatter for @ref compat32_ptr.
 *
 * Renders `(null)` for null pointers and `0x....` hex for others.
 *
 * @tparam T Referenced object type.
 */
template <typename T> struct formatter<compat32_ptr<T>> {
  /**
   * @brief No-op parse.
   */
  constexpr void parse(format_parse_context &) noexcept {}

  /**
   * @brief Renders the 32-bit pointer.
   * @param ptr The pointer to format.
   * @param out Destination sink.
   */
  void format(const compat32_ptr<T> &ptr, const sink &out) const noexcept {
    if (ptr.is_null()) {
      out.write("(null)");
    } else {
      microfmt::format_to(out, "{:#010x}", ptr.raw_value());
    }
  }
};

// ============================================================================
// Helpers for remote views
// ============================================================================

/**
 * @brief Builds a @ref remote_string_view over a 32-bit string pointer.
 * @tparam N Scratch buffer size.
 * @param ptr 32-bit pointer to the remote string.
 * @param space Address space the string lives in.
 * @param scratch Scratch buffer for copying characters.
 * @param max_limit Maximum characters to fetch.
 * @return A @ref remote_string_view bound to the pointer.
 */
template <size_t N>
[[nodiscard]] constexpr auto
make_remote_string32(compat32_ptr<const char> ptr, address_space_ref space,
                     char (&scratch)[N], size_t max_limit = 4096) noexcept {
  return remote_string_view(ptr.to_uintptr(), space, scratch, max_limit);
}

/**
 * @brief Builds a @ref remote_ref over a 32-bit object pointer.
 * @tparam T Referenced remote object type.
 * @tparam N Scratch buffer size.
 * @param ptr 32-bit pointer to the remote object.
 * @param space Address space the object lives in.
 * @param scratch Scratch buffer for loading the object.
 * @return A @ref remote_ref bound to the pointer.
 */
template <typename T, size_t N>
[[nodiscard]] constexpr auto
make_remote_ref32(compat32_ptr<T> ptr, address_space_ref space,
                  std::byte (&scratch)[N]) noexcept {
  return remote_ref<T>(ptr.to_uintptr(), space, scratch);
}

} // namespace microfmt 