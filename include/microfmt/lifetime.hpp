// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file lifetime.hpp @brief Compiler-specific lifetime, access, nullability, and Clang safe-buffers annotations. */

#include "detail/compat.hpp"
#include <type_traits>
#include <utility>

// ============================================================================
// Lifetime and Bounds Safety Attribute Macros
// ============================================================================

#if MICROFMT_HAS_CPP_ATTRIBUTE(clang::lifetimebound)
#define MICROFMT_LIFETIMEBOUND [[clang::lifetimebound]]
#elif MICROFMT_HAS_ATTRIBUTE(lifetimebound)
#define MICROFMT_LIFETIMEBOUND __attribute__((lifetimebound))
#else
#define MICROFMT_LIFETIMEBOUND
#endif

#if MICROFMT_HAS_CPP_ATTRIBUTE(gsl::owner)
#define MICROFMT_OWNER [[gsl::owner]]
#else
#define MICROFMT_OWNER
#endif

#if MICROFMT_HAS_CPP_ATTRIBUTE(gsl::pointer)
#define MICROFMT_POINTER [[gsl::pointer]]
#else
#define MICROFMT_POINTER
#endif

#if MICROFMT_HAS_CPP_ATTRIBUTE(clang::unsafe_buffer_usage)
#define MICROFMT_UNSAFE_BUFFER_USAGE [[clang::unsafe_buffer_usage]]
#elif MICROFMT_HAS_ATTRIBUTE(unsafe_buffer_usage)
#define MICROFMT_UNSAFE_BUFFER_USAGE __attribute__((unsafe_buffer_usage))
#else
#define MICROFMT_UNSAFE_BUFFER_USAGE
#endif

/** Declares that another parameter or the return value captures a borrow. */
#if MICROFMT_HAS_CPP_ATTRIBUTE(clang::lifetime_capture_by)
#define MICROFMT_LIFETIME_CAPTURE_BY(...) [[clang::lifetime_capture_by(__VA_ARGS__)]]
#elif MICROFMT_HAS_ATTRIBUTE(lifetime_capture_by)
#define MICROFMT_LIFETIME_CAPTURE_BY(...) __attribute__((lifetime_capture_by(__VA_ARGS__)))
#else
#define MICROFMT_LIFETIME_CAPTURE_BY(...)
#endif

/** Declares that a constructor or member function captures a borrow in `this`. */
#if MICROFMT_HAS_CPP_ATTRIBUTE(clang::lifetime_capture_by_this)
#define MICROFMT_LIFETIME_CAPTURE_BY_THIS [[clang::lifetime_capture_by_this]]
#elif MICROFMT_HAS_ATTRIBUTE(lifetime_capture_by_this)
#define MICROFMT_LIFETIME_CAPTURE_BY_THIS __attribute__((lifetime_capture_by_this))
#else
#define MICROFMT_LIFETIME_CAPTURE_BY_THIS MICROFMT_LIFETIME_CAPTURE_BY(this)
#endif

// ============================================================================
// Clang Safe Buffers Pragma Control Blocks
// ============================================================================

/**
 * @def MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE
 * @brief Suppresses or opts a block of code out of Clang's -Wunsafe-buffer-usage checks.
 *
 * Essential for implementing custom underlying containers, span bounds checkers,
 * or performance-critical low-level routines.
 */
/**
 * @def MICROFMT_END_UNSAFE_BUFFER_USAGE
 * @brief Closes a block opened by MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE.
 */
#if defined(__clang__)
#define MICROFMT_PRAGMA(x) _Pragma(#x)
#define MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE MICROFMT_PRAGMA(clang unsafe_buffer_usage begin)
#define MICROFMT_END_UNSAFE_BUFFER_USAGE MICROFMT_PRAGMA(clang unsafe_buffer_usage end)
#else
#define MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE
#define MICROFMT_END_UNSAFE_BUFFER_USAGE
#endif

// ============================================================================
// Nullability Attributes (GCC / Clang Static Analyzer)
// ============================================================================

#if defined(__GNUC__) || defined(__clang__)
#define MICROFMT_NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))
#else
#define MICROFMT_NONNULL(...)
#endif

// ============================================================================
// Function Purity & Constness (Optimizer & Side-Effect Safety)
// ============================================================================

#if defined(__GNUC__) || defined(__clang__)
#define MICROFMT_ATTR_PURE __attribute__((pure))
#define MICROFMT_ATTR_CONST __attribute__((const))
#else
#define MICROFMT_ATTR_PURE
#define MICROFMT_ATTR_CONST
#endif

// ============================================================================
// Compiler Buffer Access Attributes (GCC/Clang -Wstringop-overflow)
// ============================================================================

#if defined(__GNUC__) || defined(__clang__)
#define MICROFMT_ATTR_ACCESS(mode, ptr_idx) __attribute__((access(mode, ptr_idx)))
#define MICROFMT_ATTR_ACCESS_SIZE(mode, ptr_idx, size_idx) __attribute__((access(mode, ptr_idx, size_idx)))
#else
#define MICROFMT_ATTR_ACCESS(mode, ptr_idx)
#define MICROFMT_ATTR_ACCESS_SIZE(mode, ptr_idx, size_idx)
#endif

// ============================================================================
// Modern Memory & String Safety
// ============================================================================

/**
 * Marks char arrays as explicitly NOT null-terminated. Prevents compiler warnings
 * when using string functions (like strncpy) on raw GDB payload buffers.
 */
#if MICROFMT_HAS_ATTRIBUTE(nonstring)
#define MICROFMT_NONSTRING __attribute__((nonstring))
#else
#define MICROFMT_NONSTRING
#endif

/**
 * Pairs a GCC heap-like allocator with its specific deallocator for
 * `-Wmismatched-dealloc`.
 */
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 11
#define MICROFMT_MALLOC_PAIR(deallocator) __attribute__((malloc(deallocator)))
#else
#define MICROFMT_MALLOC_PAIR(deallocator)
#endif

/**
 * Informs the compiler that a returned pointer (e.g., from the scratch_allocator)
 * is guaranteed to have a specific byte alignment, allowing aggressive SIMD/vectorization optimizations.
 */
#if MICROFMT_HAS_ATTRIBUTE(assume_aligned)
#define MICROFMT_ASSUME_ALIGNED(alignment) __attribute__((assume_aligned(alignment)))
#else
#define MICROFMT_ASSUME_ALIGNED(alignment)
#endif

// ============================================================================
// Compile-Time Diagnostics (Clang Specific)
// ============================================================================

/**
 * Triggers a custom compile-time error/warning if a condition is met.
 * Perfect for preventing invalid GDB packet sizes or unaligned scratch buffers at compile time.
 * Usage: MICROFMT_DIAGNOSE_IF(size > 1024, "Packet size too large", "error")
 */
#if defined(__clang__) && MICROFMT_HAS_ATTRIBUTE(diagnose_if)
#define MICROFMT_DIAGNOSE_IF(cond, msg, type) __attribute__((diagnose_if(cond, msg, type)))
#else
#define MICROFMT_DIAGNOSE_IF(cond, msg, type)
#endif

// ============================================================================
// Control Flow & State Machine Optimizations
// ============================================================================

/**
 * Requires a guaranteed tail call when placed on a return statement.
 */
#if MICROFMT_HAS_CPP_ATTRIBUTE(clang::musttail)
#define MICROFMT_MUSTTAIL [[clang::musttail]]
#else
#define MICROFMT_MUSTTAIL
#endif

/** Marks error-handling or fallback paths (like NAK generation) to be moved out of the hot instruction cache. */
#if MICROFMT_HAS_ATTRIBUTE(cold)
#define MICROFMT_COLD __attribute__((cold))
#else
#define MICROFMT_COLD
#endif

#if MICROFMT_HAS_ATTRIBUTE(hot)
#define MICROFMT_HOT __attribute__((hot))
#else
#define MICROFMT_HOT
#endif

// ============================================================================
// Consumed State / Typestate Annotations (Clang Static Analyzer)
// ============================================================================
// These let Clang's -Wconsumed analysis track monotonic object state.

/**
 * Marks a class as having a trackable lifetime state (e.g., "unconsumed", "consumed").
 * @example class MICROFMT_CONSUMABLE(unconsumed) packet_writer { ...
 */
#if defined(__clang__) && MICROFMT_HAS_ATTRIBUTE(consumable)
#define MICROFMT_CONSUMABLE(state) __attribute__((consumable(state)))
#else
#define MICROFMT_CONSUMABLE(state)
#endif

/**
 * Restricts a method so it can only be called in a specific state.
 * @example void write() MICROFMT_CALLABLE_WHEN(unconsumed);
 */
#if defined(__clang__) && MICROFMT_HAS_ATTRIBUTE(callable_when)
#define MICROFMT_CALLABLE_WHEN(...) __attribute__((callable_when(__VA_ARGS__)))
#else
#define MICROFMT_CALLABLE_WHEN(...)
#endif

/**
 * Transitions the lifetime state of the object upon calling this method.
 * @example string_view finalize() MICROFMT_SET_TYPESTATE(consumed);
 */
#if defined(__clang__) && MICROFMT_HAS_ATTRIBUTE(set_typestate)
#define MICROFMT_SET_TYPESTATE(state) __attribute__((set_typestate(state)))
#else
#define MICROFMT_SET_TYPESTATE(state)
#endif

/**
 * Indicates what state an object is in when returned from a function.
 */
#if defined(__clang__) && MICROFMT_HAS_ATTRIBUTE(return_typestate)
#define MICROFMT_RETURN_TYPESTATE(state) __attribute__((return_typestate(state)))
#else
#define MICROFMT_RETURN_TYPESTATE(state)
#endif

// ============================================================================
// Unsafe Pointer Utilities & Unwrapping Boundaries
// ============================================================================

namespace microfmt::unsafe {

template <typename To, typename From> [[nodiscard]] constexpr To *ptr_cast(From *ptr) noexcept {
  return reinterpret_cast<To *>(ptr);
}

template <typename To, typename From> [[nodiscard]] constexpr const To *ptr_cast(const From *ptr) noexcept {
  return reinterpret_cast<const To *>(ptr);
}

template <typename T> [[nodiscard]] constexpr T *unchecked_address(T &obj) noexcept {
  return const_cast<T *>(std::addressof(obj));
}

} // namespace microfmt::unsafe