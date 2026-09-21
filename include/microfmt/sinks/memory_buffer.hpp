#pragma once
#include "microfmt/microfmt.hpp"
#include <algorithm>
#include <reloco/default_allocator.hpp>

namespace microfmt {

namespace detail {

/**
 * @brief Non-templated base class for dynamically growing buffers.
 *
 * Contains all the size, capacity, and reallocation logic to completely
 * avoid template bloat. Functions can accept a reference to this base
 * class to operate on buffers of any inline capacity.
 */
class RELOCO_POINTER memory_buffer_base {
public:
  memory_buffer_base(const memory_buffer_base &) = delete;
  memory_buffer_base &operator=(const memory_buffer_base &) = delete;

  ~memory_buffer_base() {
    if (m_data != m_inline_ptr) {
      m_alloc.deallocate(m_data, m_capacity);
    }
  }

  /**
   * @brief Creates a type-erased @ref sink adapter pointing to this instance.
   */
  [[nodiscard]] sink as_sink() noexcept RELOCO_LIFETIMEBOUND {
    return sink{
        this, [](void *ctx, microfmt::string_view sv) noexcept { static_cast<memory_buffer_base *>(ctx)->append(sv); }};
  }

  [[nodiscard]] constexpr microfmt::string_view view() const noexcept RELOCO_LIFETIMEBOUND {
    return microfmt::string_view(m_data, m_size);
  }

  [[nodiscard]] constexpr const char *data() const noexcept RELOCO_LIFETIMEBOUND { return m_data; }
  [[nodiscard]] constexpr std::size_t size() const noexcept { return m_size; }
  [[nodiscard]] constexpr std::size_t capacity() const noexcept { return m_capacity; }

protected:
  /**
   * @brief Initializes the buffer state using the derived class's stack storage.
   */
  constexpr memory_buffer_base(reloco::allocator_ref alloc, char *inline_buf, std::size_t inline_cap) noexcept
      : m_alloc(alloc), m_data(inline_buf), m_inline_ptr(inline_buf), m_size(0), m_capacity(inline_cap) {}

private:
  reloco::allocator_ref m_alloc;
  char *m_data;
  char *m_inline_ptr; // Keeps track of the stack buffer address to know when to deallocate
  std::size_t m_size;
  std::size_t m_capacity;

  void append(microfmt::string_view sv) noexcept {
    if (sv.empty())
      return;

    if (m_size + sv.size() > m_capacity) {
      if (!grow(m_size + sv.size())) {
        // Out of memory: truncate safely.
        const std::size_t avail = m_capacity - m_size;
        if (avail > 0) {
          RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
          std::copy_n(sv.data(), avail, m_data + m_size);
          RELOCO_END_UNSAFE_BUFFER_USAGE;
          m_size += avail;
        }
        return;
      }
    }

    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
    std::copy_n(sv.data(), sv.size(), m_data + m_size);
    RELOCO_END_UNSAFE_BUFFER_USAGE;
    m_size += sv.size();
  }

  [[nodiscard]] bool grow(std::size_t required_capacity) noexcept {
    std::size_t new_capacity = m_capacity + (m_capacity / 2);
    if (new_capacity < required_capacity) {
      new_capacity = required_capacity;
    }

    if (m_data == m_inline_ptr) {
      // Transition from Stack to Heap
      const auto new_mem = m_alloc.allocate(new_capacity, alignof(char));
      if (!new_mem)
        return false;

      char *new_data = static_cast<char *>(new_mem->ptr);
      RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
      std::copy_n(m_inline_ptr, m_size, new_data);
      RELOCO_END_UNSAFE_BUFFER_USAGE;

      m_data = new_data;
      m_capacity = new_capacity;
    } else {
      // Already on Heap: Try expand_in_place first
      if (const auto actual_capacity = m_alloc.expand_in_place(m_data, m_capacity, new_capacity);
          actual_capacity && actual_capacity.value() >= new_capacity) {
        m_capacity = actual_capacity.value();
      } else {
        // Fallback to standard reallocate
        const auto new_mem = m_alloc.reallocate(m_data, m_capacity, new_capacity, alignof(char));
        if (!new_mem)
          return false;

        m_data = static_cast<char *>(new_mem->ptr);
        m_capacity = new_capacity;
      }
    }
    return true;
  }
};

} // namespace detail

/**
 * @brief A dynamically growing buffer with inline stack storage.
 *
 * Inherits all logic from @ref memory_buffer_base, providing only the
 * inline stack storage array to prevent template bloat.
 *
 * @tparam InlineCapacity Number of bytes to store on the stack.
 */
template <std::size_t InlineCapacity = 512> class memory_buffer : public detail::memory_buffer_base {
public:
  explicit constexpr memory_buffer(reloco::allocator_ref alloc) noexcept
      : detail::memory_buffer_base(alloc, m_inline, InlineCapacity) {}

  constexpr memory_buffer() noexcept : memory_buffer(reloco::default_allocator()) {}

private:
  alignas(void *) char m_inline[InlineCapacity];
};

} // namespace microfmt