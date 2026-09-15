#pragma once

#include <cstddef>
#include <type_traits>

#if __has_include(<span>) && __cplusplus >= 202002L
#include <span>
#define MICROFMT_HAS_STD_SPAN 1
#else
#define MICROFMT_HAS_STD_SPAN 0
#endif

#if MICROFMT_HAS_STD_SPAN
#include <span>
#endif

namespace microfmt {

template <typename T> class span {
public:
  using element_type = T;
  using value_type = typename std::remove_cv<T>::type;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using pointer = T *;
  using const_pointer = const T *;
  using reference = T &;
  using const_reference = const T &;
  using iterator = T *;
  using const_iterator = const T *;

  /**
   * @brief Constructs an empty span with `nullptr` data and `0` size.
   */
  constexpr span() noexcept : m_ptr(nullptr), m_size(0) {}

  /**
   * @brief Constructs a span from a pointer and an explicit size.
   * @param ptr Pointer to the first element of the contiguous memory block.
   * @param size Number of elements in the buffer.
   */
  constexpr span(T *ptr, std::size_t size) noexcept
      : m_ptr(ptr), m_size(size) {}

  /**
   * @brief Constructs a span from a pointer pair (range).
   * @param first Pointer to the first element.
   * @param last Pointer to one past the last element.
   */
  constexpr span(T *first, T *last) noexcept
      : m_ptr(first), m_size(static_cast<std::size_t>(last - first)) {}

  /**
   * @brief Constructs a span from a raw C-style array.
   * @tparam N Size of the fixed array deduced at compile time.
   * @param arr Reference to the array.
   */
  template <std::size_t N>
  constexpr span(T (&arr)[N]) noexcept : m_ptr(arr), m_size(N) {}

#if MICROFMT_HAS_STD_SPAN
  /**
   * @brief Constructs a `microfmt::span` from any `std::span` (dynamic or
   * static extent).
   * @tparam U Element type (supports cv-qualifier conversion like const
   * propagation).
   * @tparam Extent The static extent of the standard span.
   */
  template <typename U, std::size_t Extent>
    requires(std::is_convertible_v<U (*)[], T (*)[]>)
  constexpr span(std::span<U, Extent> s) noexcept
      : m_ptr(s.data()), m_size(s.size()) {}

  /**
   * @brief Implicit conversion operator to `std::span<T>`.
   * @return An equivalent dynamic-extent `std::span<T>` covering the same
   * buffer.
   */
  [[nodiscard]] constexpr operator std::span<T>() const noexcept {
    return std::span<T>(m_ptr, m_size);
  }
#endif

  /**
   * @brief Returns a subspan view starting at a given offset.
   * @tparam Count Number of elements in the subspan (default = dynamic/npos).
   * @param offset Zero-based index at which the subspan begins.
   * @param count Number of elements in the subspan (defaults to remainder of
   * span).
   * @return A new `microfmt::span` view.
   */
  [[nodiscard]] constexpr span<T>
  subspan(std::size_t offset,
          std::size_t count = static_cast<std::size_t>(-1)) const noexcept {
    if (offset > m_size) {
      return span<T>(m_ptr + m_size, static_cast<std::size_t>(0));
    }
    std::size_t rem = m_size - offset;
    std::size_t actual_count = (count < rem) ? count : rem;
    return span<T>(m_ptr + offset, actual_count);
  }

  /**
   * @brief Returns a subspan with a statically specified compile-time count.
   * @tparam Count Exact number of elements requested.
   * @param offset Zero-based index at which the subspan begins.
   */
  template <std::size_t Count>
  [[nodiscard]] constexpr span<T> subspan(std::size_t offset) const noexcept {
    if (offset > m_size) {
      return span<T>(m_ptr + m_size, static_cast<std::size_t>(0));
    }
    std::size_t rem = m_size - offset;
    std::size_t actual_count = (Count < rem) ? Count : rem;
    return span<T>(m_ptr + offset, actual_count);
  }

  /**
   * @brief Returns a direct pointer to the beginning of the contiguous buffer.
   * @return Raw pointer to the elements, or `nullptr` if empty.
   */
  [[nodiscard]] constexpr T *data() const noexcept { return m_ptr; }

  /**
   * @brief Returns the number of elements in the span.
   * @return Element count.
   */
  [[nodiscard]] constexpr std::size_t size() const noexcept { return m_size; }

  /**
   * @brief Checks if the span contains zero elements.
   * @return `true` if `size() == 0`, `false` otherwise.
   */
  [[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0; }

  /**
   * @brief Accesses an element at a given index without bounds checking.
   * @param idx Zero-based index of the element to access.
   * @return Reference to the element at position @p idx.
   */
  [[nodiscard]] constexpr T &operator[](std::size_t idx) const noexcept {
    return m_ptr[idx];
  }

  /**
   * @brief Returns an iterator to the first element of the span.
   */
  [[nodiscard]] constexpr T *begin() const noexcept { return m_ptr; }

  /**
   * @brief Returns an iterator to one past the last element of the span.
   */
  [[nodiscard]] constexpr T *end() const noexcept { return m_ptr + m_size; }

  /**
   * @brief Returns a const iterator to the first element of the span.
   */
  [[nodiscard]] constexpr const T *cbegin() const noexcept { return m_ptr; }

  /**
   * @brief Returns a const iterator to one past the last element of the span.
   */
  [[nodiscard]] constexpr const T *cend() const noexcept {
    return m_ptr + m_size;
  }

private:
  T *m_ptr;
  std::size_t m_size;
};

} // namespace microfmt