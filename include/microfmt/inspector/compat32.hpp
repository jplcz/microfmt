// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

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

template <typename T> class alignas(uint32_t) compat32_ptr {
public:
  using element_type = T;

  constexpr compat32_ptr() noexcept : raw_addr_(0) {}
  constexpr compat32_ptr(std::nullptr_t) noexcept : raw_addr_(0) {}
  constexpr explicit compat32_ptr(uint32_t addr) noexcept : raw_addr_(addr) {}

  [[nodiscard]] constexpr uint32_t raw_value() const noexcept {
    return raw_addr_;
  }
  [[nodiscard]] constexpr uintptr_t to_uintptr() const noexcept {
    return static_cast<uintptr_t>(raw_addr_);
  }
  [[nodiscard]] constexpr bool is_null() const noexcept {
    return raw_addr_ == 0;
  }
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return raw_addr_ != 0;
  }

  constexpr bool operator==(const compat32_ptr &other) const noexcept {
    return raw_addr_ == other.raw_addr_;
  }
  constexpr bool operator!=(const compat32_ptr &other) const noexcept {
    return raw_addr_ != other.raw_addr_;
  }
  constexpr bool operator==(std::nullptr_t) const noexcept {
    return raw_addr_ == 0;
  }
  constexpr bool operator!=(std::nullptr_t) const noexcept {
    return raw_addr_ != 0;
  }

private:
  uint32_t raw_addr_{0};
};

static_assert(sizeof(compat32_ptr<void>) == 4,
              "compat32_ptr must be exactly 4 bytes");
static_assert(alignof(compat32_ptr<void>) == 4,
              "compat32_ptr must be 4-byte aligned");

// ============================================================================
// Formatter for compat32_ptr<T>
// ============================================================================

template <typename T> struct formatter<compat32_ptr<T>> {
  constexpr void parse(format_parse_context &) noexcept {}

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

template <size_t N>
[[nodiscard]] constexpr auto
make_remote_string32(compat32_ptr<const char> ptr, address_space_ref space,
                     char (&scratch)[N], size_t max_limit = 4096) noexcept {
  return remote_string_view(ptr.to_uintptr(), space, scratch, max_limit);
}

template <typename T, size_t N>
[[nodiscard]] constexpr auto
make_remote_ref32(compat32_ptr<T> ptr, address_space_ref space,
                  std::byte (&scratch)[N]) noexcept {
  return remote_ref<T>(ptr.to_uintptr(), space, scratch);
}

} // namespace microfmt 