#pragma once
#include "microfmt/microfmt.hpp"
#include <algorithm>
#include <reloco/default_allocator.hpp>

namespace microfmt {

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

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
  using value_type = char;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = value_type &;
  using const_reference = const value_type &;
  using pointer = value_type *;
  using const_pointer = const value_type *;
  using iterator = pointer;
  using const_iterator = const_pointer;

  memory_buffer_base(const memory_buffer_base &) = delete;
  memory_buffer_base &operator=(const memory_buffer_base &) = delete;

  RELOCO_BLOCK_RVALUE_ACCESS(memory_buffer_base);

  ~memory_buffer_base() noexcept {
    if (m_data != m_inline_ptr) {
      m_alloc.deallocate(m_data, m_capacity);
    }
  }

  /**
   * @brief Creates a type-erased @ref sink adapter pointing to this instance.
   */
  [[nodiscard]] sink as_sink() & noexcept RELOCO_LIFETIMEBOUND {
    return sink{
        this, [](void *ctx, microfmt::string_view sv) noexcept { static_cast<memory_buffer_base *>(ctx)->append(sv); }};
  }

  [[nodiscard]] constexpr microfmt::string_view view() const & noexcept RELOCO_LIFETIMEBOUND {
    return microfmt::string_view(m_data, m_size);
  }

  // --- STL Range & Capacity Observers ---
  [[nodiscard]] constexpr pointer data() & noexcept RELOCO_LIFETIMEBOUND { return m_data; }
  [[nodiscard]] constexpr const_pointer data() const & noexcept RELOCO_LIFETIMEBOUND { return m_data; }

  [[nodiscard]] constexpr iterator begin() & noexcept RELOCO_LIFETIMEBOUND { return m_data; }
  [[nodiscard]] constexpr const_iterator begin() const & noexcept RELOCO_LIFETIMEBOUND { return m_data; }
  [[nodiscard]] constexpr const_iterator cbegin() const & noexcept RELOCO_LIFETIMEBOUND { return m_data; }

  [[nodiscard]] constexpr iterator end() & noexcept RELOCO_LIFETIMEBOUND { return m_data + m_size; }
  [[nodiscard]] constexpr const_iterator end() const & noexcept RELOCO_LIFETIMEBOUND { return m_data + m_size; }
  [[nodiscard]] constexpr const_iterator cend() const & noexcept RELOCO_LIFETIMEBOUND { return m_data + m_size; }

  [[nodiscard]] constexpr size_type size() const noexcept { return m_size; }
  [[nodiscard]] constexpr size_type capacity() const noexcept { return m_capacity; }
  [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }

  [[nodiscard]] constexpr reference operator[](size_type pos) & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(pos < size(), "Index out of bounds");
    return m_data[pos];
  }
  [[nodiscard]] constexpr const_reference operator[](size_type pos) const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(pos < size(), "Index out of bounds");
    return m_data[pos];
  }

  [[nodiscard]] constexpr reference front() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "Container is empty");
    return m_data[0];
  }
  [[nodiscard]] constexpr const_reference front() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "Container is empty");
    return m_data[0];
  }
  [[nodiscard]] constexpr reference back() & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "Container is empty");
    return m_data[m_size - 1];
  }
  [[nodiscard]] constexpr const_reference back() const & noexcept RELOCO_LIFETIMEBOUND {
    RELOCO_ASSERT(!empty(), "Container is empty");
    return m_data[m_size - 1];
  }

  RELOCO_REINITIALIZES void clear() & noexcept { m_size = 0; }

  /**
   * @brief Fast-path single character append (enables std::back_inserter compatibility).
   */
  void push_back(char c) & noexcept {
    if (m_size >= m_capacity) {
      if (!grow(m_size + 1))
        return; // Truncate safely on OOM in -fno-exceptions
    }
    m_data[m_size++] = c;
  }

protected:
  /**
   * @brief Initializes the buffer state using the derived class's stack storage.
   */
  constexpr memory_buffer_base(reloco::allocator_ref alloc, char *inline_buf, std::size_t inline_cap) noexcept
      : m_alloc(alloc), m_data(inline_buf), m_inline_ptr(inline_buf), m_size(0), m_capacity(inline_cap) {}

  /**
   * @brief Move-constructs from another buffer base, resolving inline pointers.
   */
  constexpr memory_buffer_base(memory_buffer_base &&other, char *new_inline_ptr, std::size_t inline_cap) noexcept
      : m_alloc(other.m_alloc), m_data(other.m_data == other.m_inline_ptr ? new_inline_ptr : other.m_data),
        m_inline_ptr(new_inline_ptr), m_size(other.m_size), m_capacity(other.m_capacity) {

    // If the source was using its inline stack buffer, copy the bytes
    if (other.m_data == other.m_inline_ptr) {
      std::copy_n(other.m_inline_ptr, other.m_size, new_inline_ptr);
    }

    // Reset source to a safe, empty state pointing to its own stack buffer
    other.m_data = other.m_inline_ptr;
    other.m_size = 0;
    other.m_capacity = inline_cap;
  }

  /**
   * @brief Safely move-assigns the buffer, handling heap/stack transitions.
   */
  void move_assign(memory_buffer_base &&other, std::size_t inline_cap) noexcept {
    if (this == &other)
      return;

    // Free our existing heap memory if we had any
    if (m_data != m_inline_ptr) {
      m_alloc.deallocate(m_data, m_capacity);
    }

    m_alloc = other.m_alloc;
    m_size = other.m_size;
    m_capacity = other.m_capacity;

    if (other.m_data == other.m_inline_ptr) {
      // Source is on stack: copy the bytes into OUR inline buffer
      m_data = m_inline_ptr;
      std::copy_n(other.m_inline_ptr, other.m_size, m_inline_ptr);
    } else {
      // Source is on heap: steal the pointer directly
      m_data = other.m_data;
    }

    // Reset source to a safe, empty state
    other.m_data = other.m_inline_ptr;
    other.m_size = 0;
    other.m_capacity = inline_cap;
  }

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
          std::copy_n(sv.data(), avail, m_data + m_size);
          m_size += avail;
        }
        return;
      }
    }

    std::copy_n(sv.data(), sv.size(), m_data + m_size);
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
      std::copy_n(m_inline_ptr, m_size, new_data);

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

RELOCO_END_UNSAFE_BUFFER_USAGE

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

  // Move constructor delegates to base, providing the new inline stack boundary
  constexpr memory_buffer(memory_buffer &&other) noexcept
      : detail::memory_buffer_base(std::move(other), m_inline, InlineCapacity) {}

  // Move assignment delegates to base's controlled move_assign method
  memory_buffer &operator=(memory_buffer &&other) noexcept {
    this->move_assign(std::move(other), InlineCapacity);
    return *this;
  }

private:
  alignas(void *) char m_inline[InlineCapacity];
};

} // namespace microfmt

template <> struct reloco::is_trivially_relocatable<microfmt::detail::memory_buffer_base> : std::false_type {};
template <std::size_t N> struct reloco::is_trivially_relocatable<microfmt::memory_buffer<N>> : std::false_type {};
