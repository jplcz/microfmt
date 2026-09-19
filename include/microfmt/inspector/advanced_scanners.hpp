// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

/**
 * @file advanced_scanners.hpp
 * @brief Type-erased, zero-allocation memory scanners for bounded value ranges and complex data dependencies.
 *
 * Provides specialized scanners to search remote address spaces for specific conditions
 * without allocating heap memory. Includes a value range scanner to find scalars within
 * [min, max] bounds, and a dependent value scanner to evaluate sliding-window predicates
 * (useful for verifying multi-field invariants in OS thread blocks without symbols).
 */

#pragma once

#include "../microfmt.hpp"
#include "../span.hpp"
#include "address_space.hpp"
#include "memory_pattern_scanner.hpp" // For memory_scan_result and linear_memory_scanner_context
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace microfmt {

// ============================================================================
// Value Range Scanner (Finds T where min <= val <= max)
// ============================================================================

/**
 * @brief Static customization point for scanning memory for a scalar within a specific range.
 * @tparam Tag Tag identifying the scanner implementation.
 * @tparam T Trivially copyable scalar type being scanned.
 */
template <typename Tag, typename T> struct value_range_scanner_traits;

/**
 * @brief Type-erased reference for scanning memory for a scalar value within a [min, max] range.
 *
 * Abstracts the underlying memory traversal and chunking logic. Iterates over the address
 * space at a specified stride, interpreting bytes as type `T` and comparing them against bounds.
 *
 * @tparam T Trivially copyable type to interpret from memory.
 */
template <typename T> class RELOCO_POINTER value_range_scanner_ref {
public:
  static_assert(std::is_trivially_copyable_v<T>, "Scanned type must be trivially copyable.");

  struct vtable {
    expected<memory_scan_result, address_space_error> (*scan)(const void *ctx, uintptr_t start_addr,
                                                              size_t search_length, const T &min_val, const T &max_val,
                                                              size_t stride) noexcept;
  };

  constexpr value_range_scanner_ref() noexcept = default;

  template <typename Tag, typename Traits = value_range_scanner_traits<Tag, T>,
            std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  constexpr explicit value_range_scanner_ref(Tag) noexcept
      : ctx_(nullptr), vtbl_(&s_vtbl<Tag>) {}

  template <typename Tag, typename Context, typename Traits = value_range_scanner_traits<Tag, T>,
            std::enable_if_t<!std::is_void_v<typename Traits::context_type> &&
                                 std::is_convertible_v<const Context *,
                                                       const typename Traits::context_type *>,
                             int> = 0>
  constexpr value_range_scanner_ref(
      Tag, const Context &ctx RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag>) {}

  template <typename Tag, typename Context, std::enable_if_t<!std::is_lvalue_reference_v<Context>, int> = 0>
  constexpr value_range_scanner_ref(Tag, Context &&) = delete;

  template <typename Tag, typename Traits = value_range_scanner_traits<Tag, T>,
            std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr value_range_scanner_ref make() noexcept {
    return value_range_scanner_ref(Tag{});
  }

  template <typename Tag, typename Context, typename Traits = value_range_scanner_traits<Tag, T>,
            std::enable_if_t<!std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr value_range_scanner_ref
  make(const Context &context RELOCO_LIFETIMEBOUND) noexcept {
    return value_range_scanner_ref(Tag{}, context);
  }

  /**
   * @brief Scans memory for a value falling within the inclusive bounds [min_val, max_val].
   * @param start_addr Absolute memory address to begin scanning.
   * @param search_length Maximum number of bytes to inspect.
   * @param min_val Minimum acceptable value (inclusive).
   * @param max_val Maximum acceptable value (inclusive).
   * @param stride Byte alignment to step forward after each window check.
   */
  [[nodiscard]] expected<memory_scan_result, address_space_error> scan(uintptr_t start_addr, size_t search_length,
                                                                       const T &min_val, const T &max_val,
                                                                       size_t stride = alignof(T)) const noexcept {
    if (!vtbl_)
      return unexpected(address_space_error::invalid_handle);
    return vtbl_->scan(ctx_.get(), start_addr, search_length, min_val, max_val, stride);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return vtbl_ != nullptr; }

private:
  template <typename Tag>
  static expected<memory_scan_result, address_space_error> scan_entry(const void *context, uintptr_t start_addr,
                                                                      size_t search_length, const T &min_val,
                                                                      const T &max_val, size_t stride) noexcept {
    using context_type = typename value_range_scanner_traits<Tag, T>::context_type;
    if constexpr (std::is_void_v<context_type>) {
      return value_range_scanner_traits<Tag, T>::scan(
          start_addr, search_length, min_val, max_val, stride);
    } else {
      const auto &typed_context = *static_cast<const context_type *>(context);
      return value_range_scanner_traits<Tag, T>::scan(
          value_ref<const context_type>(typed_context), start_addr,
          search_length, min_val, max_val, stride);
    }
  }

  template <typename Tag> static constexpr vtable s_vtbl{&scan_entry<Tag>};

  value_ptr<const void> ctx_{};
  const vtable *vtbl_{nullptr};
};

/**
 * @brief Typed wrapper owning the context for a value range scanner.
 *
 * Retains state necessary for the scanning operation and provides a type-erased
 * `value_range_scanner_ref` for interoperability with generic scanning algorithms.
 *
 * @tparam Tag Tag identifying the scanner implementation.
 * @tparam T Trivially copyable type to interpret from memory.
 */
template <typename Tag, typename T,
          bool Stateless =
              std::is_void_v<typename value_range_scanner_traits<Tag, T>::context_type>>
class value_range_scanner;

template <typename Tag, typename T>
class RELOCO_OWNER value_range_scanner<Tag, T, false> {
public:
  using traits_type = value_range_scanner_traits<Tag, T>;
  using context_type = typename value_range_scanner_traits<Tag, T>::context_type;

  constexpr explicit value_range_scanner(context_type context) noexcept : context_(std::move(context)) {}

  [[nodiscard]] constexpr value_ref<context_type>
  context() & noexcept RELOCO_LIFETIMEBOUND {
    return value_ref<context_type>(context_);
  }

  [[nodiscard]] constexpr value_ref<const context_type>
  context() const & noexcept RELOCO_LIFETIMEBOUND {
    return value_ref<const context_type>(context_);
  }

  [[nodiscard]] constexpr value_range_scanner_ref<T> ref() const & noexcept RELOCO_LIFETIMEBOUND {
    return value_range_scanner_ref<T>(Tag{}, context_);
  }

  [[nodiscard]] constexpr operator value_range_scanner_ref<T>()
      const & noexcept RELOCO_LIFETIMEBOUND {
    return ref();
  }

  value_ref<context_type> context() && = delete;
  value_ref<const context_type> context() const && = delete;
  value_range_scanner_ref<T> ref() const && = delete;
  operator value_range_scanner_ref<T>() const && = delete;

private:
  context_type context_;
};

template <typename Tag, typename T>
class value_range_scanner<Tag, T, true> {
public:
  using traits_type = value_range_scanner_traits<Tag, T>;
  using context_type = void;

  [[nodiscard]] static constexpr value_range_scanner_ref<T> ref() noexcept {
    return value_range_scanner_ref<T>(Tag{});
  }
};

// ============================================================================
// Dependent Value Scanner (Custom predicate over a sliding window)
// ============================================================================

/**
 * @brief Function pointer type for evaluating a logical predicate over a memory window.
 * @param window_bytes Pointer to the current memory window chunk.
 * @param user_context Opaque pointer to caller-provided state.
 * @return True if the memory window satisfies the predicate.
 */
using dependent_predicate_fn = bool (*)(const void *window_bytes, void *user_context) noexcept;

/**
 * @brief Static customization point for scanning memory using a sliding-window predicate.
 * @tparam Tag Tag identifying the scanner implementation.
 */
template <typename Tag> struct dependent_value_scanner_traits;

/**
 * @brief Type-erased reference for scanning memory using a custom logical predicate.
 *
 * Iterates a sliding window over the remote address space, invoking a caller-provided
 * callback on each chunk. Ideal for locating complex data structures (like OS control
 * blocks) by validating internal constraints (e.g., magic numbers combined with
 * bounds-checked enums) when debugging symbols are unavailable.
 */
class RELOCO_POINTER dependent_value_scanner_ref {
public:
  struct vtable {
    expected<memory_scan_result, address_space_error> (*scan)(const void *ctx, uintptr_t start_addr,
                                                              size_t search_length, size_t window_size, size_t stride,
                                                              dependent_predicate_fn predicate,
                                                              void *user_context) noexcept;
  };

  constexpr dependent_value_scanner_ref() noexcept = default;

  template <typename Tag, typename Traits = dependent_value_scanner_traits<Tag>,
            std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  constexpr explicit dependent_value_scanner_ref(Tag) noexcept
      : ctx_(nullptr), vtbl_(&s_vtbl<Tag>) {}

  template <typename Tag, typename Context, typename Traits = dependent_value_scanner_traits<Tag>,
            std::enable_if_t<!std::is_void_v<typename Traits::context_type> &&
                                 std::is_convertible_v<const Context *,
                                                       const typename Traits::context_type *>,
                             int> = 0>
  constexpr dependent_value_scanner_ref(
      Tag, const Context &ctx RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag>) {}

  template <typename Tag, typename Context, std::enable_if_t<!std::is_lvalue_reference_v<Context>, int> = 0>
  constexpr dependent_value_scanner_ref(Tag, Context &&) = delete;

  template <typename Tag, typename Traits = dependent_value_scanner_traits<Tag>,
            std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr dependent_value_scanner_ref make() noexcept {
    return dependent_value_scanner_ref(Tag{});
  }

  template <typename Tag, typename Context, typename Traits = dependent_value_scanner_traits<Tag>,
            std::enable_if_t<!std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr dependent_value_scanner_ref
  make(const Context &context RELOCO_LIFETIMEBOUND) noexcept {
    return dependent_value_scanner_ref(Tag{}, context);
  }

  /**
   * @brief Scans memory using a custom sliding-window callback.
   * @param start_addr Absolute memory address to begin scanning.
   * @param search_length Maximum number of bytes to inspect.
   * @param window_size Byte width passed to the predicate function.
   * @param stride Byte alignment to step forward after each window check.
   * @param predicate Callback returning true if the window contents match the target structure.
   * @param user_context Opaque pointer forwarded to the predicate.
   */
  [[nodiscard]] expected<memory_scan_result, address_space_error> scan(uintptr_t start_addr, size_t search_length,
                                                                       size_t window_size, size_t stride,
                                                                       dependent_predicate_fn predicate,
                                                                       void *user_context = nullptr) const noexcept {
    if (!vtbl_)
      return unexpected(address_space_error::invalid_handle);
    return vtbl_->scan(ctx_.get(), start_addr, search_length, window_size, stride, predicate, user_context);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return vtbl_ != nullptr; }

private:
  template <typename Tag>
  static expected<memory_scan_result, address_space_error>
  scan_entry(const void *context, uintptr_t start_addr, size_t search_length, size_t window_size, size_t stride,
             dependent_predicate_fn predicate, void *user_context) noexcept {
    using context_type = typename dependent_value_scanner_traits<Tag>::context_type;
    if constexpr (std::is_void_v<context_type>) {
      return dependent_value_scanner_traits<Tag>::scan(
          start_addr, search_length, window_size, stride, predicate,
          user_context);
    } else {
      const auto &typed_context = *static_cast<const context_type *>(context);
      return dependent_value_scanner_traits<Tag>::scan(
          value_ref<const context_type>(typed_context), start_addr,
          search_length, window_size, stride, predicate, user_context);
    }
  }

  template <typename Tag> static constexpr vtable s_vtbl{&scan_entry<Tag>};

  value_ptr<const void> ctx_{};
  const vtable *vtbl_{nullptr};
};

/**
 * @brief Typed wrapper owning the context for a dependent value scanner.
 *
 * Retains state necessary for sliding-window memory traversal and provides a type-erased
 * `dependent_value_scanner_ref` for interoperability.
 *
 * @tparam Tag Tag identifying the scanner implementation.
 */
template <typename Tag,
          bool Stateless =
              std::is_void_v<typename dependent_value_scanner_traits<Tag>::context_type>>
class dependent_value_scanner;

template <typename Tag>
class RELOCO_OWNER dependent_value_scanner<Tag, false> {
public:
  using traits_type = dependent_value_scanner_traits<Tag>;
  using context_type = typename dependent_value_scanner_traits<Tag>::context_type;

  constexpr explicit dependent_value_scanner(context_type context) noexcept : context_(std::move(context)) {}

  [[nodiscard]] constexpr value_ref<context_type>
  context() & noexcept RELOCO_LIFETIMEBOUND {
    return value_ref<context_type>(context_);
  }

  [[nodiscard]] constexpr value_ref<const context_type>
  context() const & noexcept RELOCO_LIFETIMEBOUND {
    return value_ref<const context_type>(context_);
  }

  [[nodiscard]] constexpr dependent_value_scanner_ref ref() const & noexcept RELOCO_LIFETIMEBOUND {
    return dependent_value_scanner_ref(Tag{}, context_);
  }

  [[nodiscard]] constexpr operator dependent_value_scanner_ref()
      const & noexcept RELOCO_LIFETIMEBOUND {
    return ref();
  }

  value_ref<context_type> context() && = delete;
  value_ref<const context_type> context() const && = delete;
  dependent_value_scanner_ref ref() const && = delete;
  operator dependent_value_scanner_ref() const && = delete;

private:
  context_type context_;
};

template <typename Tag>
class dependent_value_scanner<Tag, true> {
public:
  using traits_type = dependent_value_scanner_traits<Tag>;
  using context_type = void;

  [[nodiscard]] static constexpr dependent_value_scanner_ref ref() noexcept {
    return dependent_value_scanner_ref(Tag{});
  }
};

// ============================================================================
// Standard Implementations (Backed by linear_memory_scanner_context)
// ============================================================================

template <typename T> struct linear_value_range_scanner_tag {};
struct linear_dependent_value_scanner_tag {};

// Range Scanner Traits
template <typename T> struct value_range_scanner_traits<linear_value_range_scanner_tag<T>, T> {
  using context_type = linear_memory_scanner_context;

  static expected<memory_scan_result, address_space_error> scan(value_ref<const context_type> ctx, uintptr_t start_addr,
                                                                size_t search_length, const T &min_val,
                                                                const T &max_val, size_t stride) noexcept {
    if (stride == 0)
      stride = 1;
    const size_t window_size = sizeof(T);
    if (ctx->scratch.size() < window_size)
      return unexpected(address_space_error::invalid_buffer);
    if (!ctx->space)
      return unexpected(address_space_error::invalid_handle);

    uintptr_t current_addr = start_addr;
    size_t remaining = search_length;

    while (remaining >= window_size) {
      size_t to_read = (remaining < ctx->scratch.size()) ? remaining : ctx->scratch.size();
      auto read_res = ctx->space.read_bytes(current_addr, ctx->scratch.data(), to_read);
      if (!read_res)
        return unexpected(read_res.error());

      size_t search_limit = to_read - window_size;
      for (size_t offset = 0; offset <= search_limit; offset += stride) {
        T val;
        RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
        std::memcpy(&val, ctx->scratch.data() + offset, sizeof(T));
        RELOCO_END_UNSAFE_BUFFER_USAGE;
        if (val >= min_val && val <= max_val) {
          if (current_addr >
              std::numeric_limits<uintptr_t>::max() - offset) {
            return unexpected(address_space_error::invalid_address);
          }
          return memory_scan_result{true, current_addr + offset};
        }
      }

      size_t advance = ((search_limit) / stride) * stride + stride;
      if (current_addr >
          std::numeric_limits<uintptr_t>::max() - advance) {
        return unexpected(address_space_error::invalid_address);
      }
      current_addr += advance;
      remaining = (advance > remaining) ? 0 : remaining - advance;
    }
    return memory_scan_result{false, 0};
  }
};

// Dependent Scanner Traits
template <> struct dependent_value_scanner_traits<linear_dependent_value_scanner_tag> {
  using context_type = linear_memory_scanner_context;

  static expected<memory_scan_result, address_space_error> scan(value_ref<const context_type> ctx, uintptr_t start_addr,
                                                                size_t search_length, size_t window_size, size_t stride,
                                                                dependent_predicate_fn predicate,
                                                                void *user_context) noexcept {
    if (stride == 0)
      stride = 1;
    if (window_size == 0)
      return unexpected(address_space_error::invalid_buffer);
    if (ctx->scratch.size() < window_size)
      return unexpected(address_space_error::invalid_buffer);
    if (!ctx->space)
      return unexpected(address_space_error::invalid_handle);
    if (!predicate)
      return unexpected(address_space_error::invalid_buffer);

    uintptr_t current_addr = start_addr;
    size_t remaining = search_length;

    while (remaining >= window_size) {
      size_t to_read = (remaining < ctx->scratch.size()) ? remaining : ctx->scratch.size();
      auto read_res = ctx->space.read_bytes(current_addr, ctx->scratch.data(), to_read);
      if (!read_res)
        return unexpected(read_res.error());

      size_t search_limit = to_read - window_size;
      for (size_t offset = 0; offset <= search_limit; offset += stride) {
        RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
        const void *window = ctx->scratch.data() + offset;
        RELOCO_END_UNSAFE_BUFFER_USAGE;
        if (predicate(window, user_context)) {
          if (current_addr >
              std::numeric_limits<uintptr_t>::max() - offset) {
            return unexpected(address_space_error::invalid_address);
          }
          return memory_scan_result{true, current_addr + offset};
        }
      }

      size_t advance = ((search_limit) / stride) * stride + stride;
      if (current_addr >
          std::numeric_limits<uintptr_t>::max() - advance) {
        return unexpected(address_space_error::invalid_address);
      }
      current_addr += advance;
      remaining = (advance > remaining) ? 0 : remaining - advance;
    }
    return memory_scan_result{false, 0};
  }
};

} // namespace microfmt