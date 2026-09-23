// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

/**
 * @file memory_pattern_scanner.hpp
 * @brief Type-erased, zero-allocation memory pattern scanner for exact and masked byte sequences.
 *
 * Provides a framework for searching remote address spaces for specific byte patterns
 * without allocating heap memory. It uses a chunked, sliding-window approach over a
 * caller-supplied scratch buffer to safely traverse memory while properly handling
 * patterns that cross chunk boundaries.
 */

#pragma once

#include "../microfmt.hpp"
#include "../reloco.hpp"
#include "address_space.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace microfmt {

/**
 * @brief Result of a memory pattern scan operation.
 */
struct MICROFMT_API_CLASS memory_scan_result {
  /** @brief True if the pattern or condition was successfully found. */
  bool found;
  /** @brief The absolute memory address where the match begins (valid only if found is true). */
  uintptr_t address;
};

// ============================================================================
// Provider Traits Definition
// ============================================================================

/**
 * @brief Static customization point for memory pattern scanners.
 *
 * Specialize this trait for a specific tag to provide the underlying
 * memory scanning logic.
 *
 * @tparam Tag Tag identifying the scanner implementation.
 */
template <typename Tag> struct memory_pattern_scanner_traits;

// ============================================================================
// Type-Erased Reference
// ============================================================================

/**
 * @brief Type-erased, zero-allocation handle to a memory pattern scanner.
 *
 * Borrows the scanner context and exposes a generic `scan` operation,
 * abstracting the underlying memory traversal and chunking mechanics.
 */
class RELOCO_POINTER memory_pattern_scanner_ref {
public:
  struct vtable {
    expected<memory_scan_result, address_space_error> (*scan)(const void *ctx, uintptr_t start_addr,
                                                              size_t search_length, span<const std::byte> pattern,
                                                              span<const std::byte> mask) noexcept;
  };

  /** @brief Constructs an empty (invalid) handle. */
  constexpr memory_pattern_scanner_ref() noexcept = default;

  /**
   * @brief Constructs a handle for a stateless scanner tag.
   */
  template <typename Tag, typename Traits = memory_pattern_scanner_traits<Tag>,
            std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  constexpr explicit memory_pattern_scanner_ref(Tag) noexcept : ctx_(nullptr), vtbl_(&s_vtbl<Tag>) {}

  /**
   * @brief Constructs a handle for a stateful scanner tag.
   * @param ctx Context object providing the memory reading capabilities.
   */
  template <typename Tag, typename Context, typename Traits = memory_pattern_scanner_traits<Tag>,
            std::enable_if_t<!std::is_void_v<typename Traits::context_type> &&
                                 std::is_convertible_v<const Context *, const typename Traits::context_type *>,
                             int> = 0>
  constexpr memory_pattern_scanner_ref(
      Tag, const Context &ctx RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag>) {}

  template <typename Tag, typename Context, std::enable_if_t<!std::is_lvalue_reference_v<Context>, int> = 0>
  constexpr memory_pattern_scanner_ref(Tag, Context &&) = delete;

  template <typename Tag, typename Traits = memory_pattern_scanner_traits<Tag>,
            std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr memory_pattern_scanner_ref make() noexcept {
    return memory_pattern_scanner_ref(Tag{});
  }

  template <typename Tag, typename Context, typename Traits = memory_pattern_scanner_traits<Tag>,
            std::enable_if_t<!std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr memory_pattern_scanner_ref make(const Context &ctx RELOCO_LIFETIMEBOUND) noexcept {
    return memory_pattern_scanner_ref(Tag{}, ctx);
  }

  template <typename Tag, typename Context, std::enable_if_t<!std::is_lvalue_reference_v<Context>, int> = 0>
  static memory_pattern_scanner_ref make(Context &&) = delete;

  /**
   * @brief Executes a type-erased pattern scan across remote memory.
   *
   * @param start_addr Absolute memory address to begin searching.
   * @param search_length Maximum number of bytes to search.
   * @param pattern The specific byte sequence to find.
   * @param mask Optional bitmask of the same size as `pattern`. Bits set to 1 are matched;
   *             bits set to 0 are ignored (wildcards). If empty, performs an exact match.
   * @return Result containing the matching address if found, or an address space error on read failure.
   */
  [[nodiscard]] expected<memory_scan_result, address_space_error> scan(uintptr_t start_addr, size_t search_length,
                                                                       span<const std::byte> pattern,
                                                                       span<const std::byte> mask = {}) const noexcept {
    if (!vtbl_)
      return unexpected(address_space_error::invalid_handle);
    return vtbl_->scan(ctx_.get(), start_addr, search_length, pattern, mask);
  }

  /** @brief Reports whether the handle is bound to a valid scanner context. */
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return vtbl_ != nullptr; }

private:
  template <typename Tag>
  static expected<memory_scan_result, address_space_error>
  scan_entry(const void *context, uintptr_t start_addr, size_t search_length, span<const std::byte> pattern,
             span<const std::byte> mask) noexcept {
    using context_type = typename memory_pattern_scanner_traits<Tag>::context_type;
    if constexpr (std::is_void_v<context_type>) {
      return memory_pattern_scanner_traits<Tag>::scan(start_addr, search_length, pattern, mask);
    } else {
      const auto &typed_context = *static_cast<const context_type *>(context);
      return memory_pattern_scanner_traits<Tag>::scan(value_ref<const context_type>(typed_context), start_addr,
                                                      search_length, pattern, mask);
    }
  }

  template <typename Tag> static constexpr vtable s_vtbl{&scan_entry<Tag>};

  value_ptr<const void> ctx_{};
  const vtable *vtbl_{nullptr};
};

// ============================================================================
// Typed Wrapper
// ============================================================================

template <typename Tag, bool Stateless = std::is_void_v<typename memory_pattern_scanner_traits<Tag>::context_type>>
class memory_pattern_scanner;

/**
 * @brief Typed wrapper owning the stateful context for a memory pattern scanner.
 *
 * @tparam Tag Tag identifying the scanner implementation.
 */
template <typename Tag> class RELOCO_OWNER memory_pattern_scanner<Tag, false> {
public:
  using traits_type = memory_pattern_scanner_traits<Tag>;
  using context_type = typename traits_type::context_type;

  constexpr explicit memory_pattern_scanner(context_type context) noexcept : context_(std::move(context)) {}

  [[nodiscard]] constexpr value_ref<context_type> context() & noexcept RELOCO_LIFETIMEBOUND {
    return value_ref<context_type>(context_);
  }

  [[nodiscard]] constexpr value_ref<const context_type> context() const & noexcept RELOCO_LIFETIMEBOUND {
    return value_ref<const context_type>(context_);
  }

  /** @brief Creates a type-erased reference borrowing this scanner's context. */
  [[nodiscard]] constexpr memory_pattern_scanner_ref ref() const & noexcept RELOCO_LIFETIMEBOUND {
    return memory_pattern_scanner_ref(Tag{}, context_);
  }

  value_ref<context_type> context() && = delete;
  value_ref<const context_type> context() const && = delete;
  memory_pattern_scanner_ref ref() const && = delete;

private:
  context_type context_;
};

/**
 * @brief Typed wrapper for a stateless memory pattern scanner.
 *
 * @tparam Tag Tag identifying the scanner implementation.
 */
template <typename Tag> class memory_pattern_scanner<Tag, true> {
public:
  [[nodiscard]] static constexpr memory_pattern_scanner_ref ref() noexcept { return memory_pattern_scanner_ref(Tag{}); }
};

// ============================================================================
// Standard Linear Scanner Implementation
// ============================================================================

/** @brief Tag identifying the standard linear, sliding-window memory scanner. */
struct linear_memory_scanner_tag {};

/** @brief State retained by the standard linear memory scanner. */
struct MICROFMT_API_CLASS linear_memory_scanner_context {
  /** @brief Target memory address space to scan. */
  address_space_ref space;
  /** @brief Caller-provided chunk buffer used for the sliding window. */
  span<std::byte> scratch;

  constexpr linear_memory_scanner_context(address_space_ref space_ref,
                                          span<std::byte> scratch_buf RELOCO_LIFETIMEBOUND
                                              RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : space(space_ref), scratch(scratch_buf) {}
};

/**
 * @brief Implementation of the memory pattern scanning logic.
 */
template <> struct memory_pattern_scanner_traits<linear_memory_scanner_tag> {
  using context_type = linear_memory_scanner_context;

  static expected<memory_scan_result, address_space_error> scan(value_ref<const context_type> ctx, uintptr_t start_addr,
                                                                size_t search_length, span<const std::byte> pattern,
                                                                span<const std::byte> mask) noexcept {
    if (pattern.empty()) {
      return memory_scan_result{true, start_addr};
    }
    if (ctx->scratch.size() < pattern.size()) {
      return unexpected(address_space_error::invalid_buffer);
    }
    if (!mask.empty() && mask.size() != pattern.size()) {
      return unexpected(address_space_error::invalid_buffer);
    }
    if (!ctx->space) {
      return unexpected(address_space_error::invalid_handle);
    }

    uintptr_t current_addr = start_addr;
    size_t remaining_search = search_length;

    // Iterate through memory using the scratch buffer as a sliding window
    while (remaining_search >= pattern.size()) {
      size_t to_read = (remaining_search < ctx->scratch.size()) ? remaining_search : ctx->scratch.size();

      auto read_res = ctx->space.read_bytes(current_addr, ctx->scratch.data(), to_read);
      if (!read_res) {
        // Stop on first read error (e.g., reaching unmapped memory)
        return unexpected(read_res.error());
      }

      size_t search_limit = to_read - pattern.size();
      for (size_t i = 0; i <= search_limit; ++i) {
        if (check_match(ctx->scratch.subspan(i), pattern, mask)) {
          if (current_addr > std::numeric_limits<uintptr_t>::max() - i) {
            return unexpected(address_space_error::invalid_address);
          }
          return memory_scan_result{true, current_addr + i};
        }
      }

      // Advance the window, keeping (pattern.size() - 1) overlap to catch cross-chunk matches
      size_t advance = to_read - pattern.size() + 1;
      if (current_addr >
          std::numeric_limits<uintptr_t>::max() - advance) {
        return unexpected(address_space_error::invalid_address);
      }
      current_addr += advance;
      remaining_search -= advance;
    }

    return memory_scan_result{false, 0};
  }

private:
  [[nodiscard]] static bool check_match(span<const std::byte> mem_window, span<const std::byte> pattern,
                                        span<const std::byte> mask) noexcept {
    for (size_t i = 0; i < pattern.size(); ++i) {
      if (!mask.empty()) {
        if ((mem_window[i] & mask[i]) != (pattern[i] & mask[i])) {
          return false;
        }
      } else {
        if (mem_window[i] != pattern[i]) {
          return false;
        }
      }
    }
    return true;
  }
};

} // namespace microfmt