// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "lifetime.hpp"
#include "span.hpp"
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

// Alignment and bump allocation require audited pointer arithmetic inside this
// checked allocation boundary.
MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE

namespace microfmt {

/**
 * @brief Zero-allocation bump allocator for scratch buffers.
 *
 * Restricts object placement strictly to trivially destructible types to avoid
 * manual destructor tracking in low-stack, bare-metal environments. Supports
 * partitioning remaining capacity to pass downstream.
 */
class MICROFMT_POINTER scratch_allocator {
public:
  /**
   * @brief Constructs a scratch allocator over a provided byte span.
   * @param buffer Caller-owned backing memory storage.
   */
  explicit constexpr scratch_allocator(
      span<std::byte> buffer MICROFMT_LIFETIMEBOUND MICROFMT_LIFETIME_CAPTURE_BY_THIS) noexcept
      : m_buf(buffer) {}

  /**
   * @brief Constructs a scratch allocator over a provided character span.
   * @param buffer Caller-owned backing memory storage.
   */
  explicit scratch_allocator(span<char> buffer MICROFMT_LIFETIMEBOUND MICROFMT_LIFETIME_CAPTURE_BY_THIS) noexcept
      : m_buf(reinterpret_cast<std::byte *>(buffer.data()), buffer.size()) {}

  /**
   * @brief Reserves aligned storage for objects without constructing them.
   * @tparam T Object type requiring storage.
   * @param count Number of contiguous objects to reserve.
   * @return Pointer to the aligned storage, or `nullptr` if it does not fit.
   */
  template <typename T>
  [[nodiscard]] MICROFMT_ASSUME_ALIGNED(alignof(T)) T *allocate(size_t count = 1) noexcept MICROFMT_LIFETIMEBOUND {
    if (count == 0 || count > available() / sizeof(T)) {
      return nullptr;
    }

    const size_t size = count * sizeof(T);
    std::byte *current_ptr = m_buf.data() + m_pos;
    const uintptr_t addr = reinterpret_cast<uintptr_t>(current_ptr);
    const size_t alignment_offset = static_cast<size_t>((alignof(T) - (addr % alignof(T))) % alignof(T));

    if (alignment_offset > available() || size > available() - alignment_offset) {
      return nullptr;
    }

    m_pos += alignment_offset + size;
    return static_cast<T *>(static_cast<void *>(current_ptr + alignment_offset));
  }

  /**
   * @brief Places a trivially destructible object of type T into the scratch buffer.
   *
   * Enforces `std::is_trivially_destructible_v<T>` at compile-time. Handles
   * proper type alignment and updates the internal bump offset.
   *
   * @tparam T The trivially destructible type to construct.
   * @tparam Args Constructor argument types.
   * @param args Arguments forwarded to T's constructor.
   * @return Pointer to the newly constructed object, or `nullptr` if out of space.
   */
  template <typename T, typename... Args>
  [[nodiscard]] MICROFMT_ASSUME_ALIGNED(alignof(T)) T *create(Args &&...args) noexcept MICROFMT_LIFETIMEBOUND {
    static_assert(std::is_trivially_destructible_v<T>, "scratch_allocator only permits trivially destructible types");

    T *storage = allocate<T>();
    if (storage == nullptr) {
      return nullptr;
    }
    return ::new (static_cast<void *>(storage)) T(std::forward<Args>(args)...);
  }

  /**
   * @brief Creates and returns a new scratch_allocator owning the remaining unallocated space.
   *
   * Useful for passing the "rest" of the buffer downstream to nested renderers.
   *
   * @return A child scratch_allocator wrapping the remaining memory.
   */
  [[nodiscard]] constexpr scratch_allocator rest() const noexcept MICROFMT_LIFETIMEBOUND {
    return scratch_allocator{remaining_span()};
  }

  /**
   * @brief Returns a span over the remaining unallocated space.
   */
  [[nodiscard]] constexpr span<std::byte> remaining_span() const noexcept MICROFMT_LIFETIMEBOUND {
    if (m_pos >= m_buf.size()) {
      return {};
    }
    return {m_buf.data() + m_pos, m_buf.size() - m_pos};
  }

  /**
   * @brief Resets the allocator back to the beginning of the buffer.
   */
  constexpr void reset() noexcept { m_pos = 0; }

  [[nodiscard]] constexpr size_t size() const noexcept { return m_pos; }
  [[nodiscard]] constexpr size_t capacity() const noexcept { return m_buf.size(); }
  [[nodiscard]] constexpr size_t available() const noexcept {
    return (m_pos < m_buf.size()) ? (m_buf.size() - m_pos) : 0;
  }

private:
  span<std::byte> m_buf;
  size_t m_pos{0};
};

} // namespace microfmt

MICROFMT_END_UNSAFE_BUFFER_USAGE