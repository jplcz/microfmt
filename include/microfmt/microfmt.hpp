#pragma once

/** @file microfmt.hpp @brief Core formatting primitives, sinks, and customization point. */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#if __has_include(<span>) && __cplusplus >= 202002L
#include <span>
#define MICROFMT_HAS_STD_SPAN 1
#else
#define MICROFMT_HAS_STD_SPAN 0
#endif

namespace microfmt {

// ============================================================================
// Minimal Span Replacement
// ============================================================================

/**
 * @brief A lightweight, non-owning contiguous view over a sequence of elements.
 *
 * Designed as a zero-allocation, minimal-footprint alternative to @c std::span
 * that works seamlessly across C++17 and C++20 freestanding/embedded targets.
 *
 * @tparam T The element type stored in the contiguous buffer.
 */
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
   * @brief Constructs a span from a raw C-style array.
   * @tparam N Size of the fixed array deduced at compile time.
   * @param arr Reference to the array.
   */
  template <std::size_t N>
  constexpr span(T (&arr)[N]) noexcept : m_ptr(arr), m_size(N) {}

#if MICROFMT_HAS_STD_SPAN
  /**
   * @brief Constructs a `microfmt::span` from a standard `std::span`.
   * @tparam Extent The static extent of the standard span.
   * @param s The `std::span` instance to construct from.
   */
  template <std::size_t Extent>
  constexpr span(std::span<T, Extent> s) noexcept
      : m_ptr(s.data()), m_size(s.size()) {}

  /**
   * @brief Implicit conversion operator to `std::span<T>`.
   * @return An equivalent `std::span<T>` covering the same buffer.
   */
  [[nodiscard]] constexpr operator std::span<T>() const noexcept {
    return std::span<T>(m_ptr, m_size);
  }
#endif

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

// ============================================================================
// Type-Erased Callback Sink
// ============================================================================

/**
 * @brief Type-erased, zero-allocation output sink.
 *
 * Encapsulates a user-provided context pointer and a stateless write callback
 * function to stream formatted characters without dynamic allocations, virtual
 * dispatch, or RTTI overhead.
 */
struct sink {
  /**
   * @brief Function pointer signature for the write callback.
   *
   * @param ctx Opaque user context pointer passed through from the sink.
   * @param sv  Non-owning view of the character slice to write.
   */
  using write_fn_t = void (*)(void *ctx, std::string_view sv) noexcept;

  /**
   * @brief Opaque pointer to caller-defined state/context.
   */
  void *ctx{nullptr};

  /**
   * @brief Callback function invoked when output is written.
   */
  write_fn_t write_fn{nullptr};

  /**
   * @brief Emits a sequence of characters to the underlying output sink.
   *
   * If @p sv is empty or @ref write_fn is @c nullptr, this operation is a
   * no-op.
   *
   * @param sv String view containing characters to write.
   */
  void write(std::string_view sv) const noexcept {
    if (write_fn && !sv.empty()) {
      write_fn(ctx, sv);
    }
  }

  /**
   * @brief Emits a single character to the underlying output sink.
   *
   * @param c Character to write.
   */
  void put(char c) const noexcept { write(std::string_view(&c, 1)); }
};

// ============================================================================
// Concrete Sinks
// ============================================================================

/**
 * @brief Output sink that writes formatted characters into a fixed-size
 * contiguous memory buffer.
 *
 * Wraps an externally provided buffer (via @ref microfmt::span or @c std::span)
 * and exposes a non-owning, zero-allocation @ref sink interface. Writes that
 * exceed the remaining capacity are truncated safely.
 */
class span_sink {
public:
  /**
   * @brief Constructs a span sink over a @ref microfmt::span of character
   * storage.
   *
   * @param buf The target character buffer view.
   */
  explicit constexpr span_sink(span<char> buf) noexcept
      : m_buf(buf), m_pos(0) {}

#if MICROFMT_HAS_STD_SPAN
  /**
   * @brief Constructs a span sink over a @c std::span of character storage.
   *
   * @tparam Extent The static extent of the standard span.
   * @param buf The target standard span view.
   */
  template <std::size_t Extent>
  explicit constexpr span_sink(std::span<char, Extent> buf) noexcept
      : m_buf(buf.data(), buf.size()), m_pos(0) {}
#endif

  /**
   * @brief Creates a type-erased @ref sink adapter pointing to this instance.
   *
   * @return A lightweight @ref sink struct configured with a callback to append
   * to this buffer.
   */
  [[nodiscard]] sink as_sink() noexcept {
    return sink{this, [](void *ctx, std::string_view sv) noexcept {
                  auto *self = static_cast<span_sink *>(ctx);
                  const size_t avail = (self->m_pos < self->m_buf.size())
                                           ? (self->m_buf.size() - self->m_pos)
                                           : 0;
                  const size_t n = std::min(sv.size(), avail);
                  if (n > 0) {
                    std::copy_n(sv.data(), n, self->m_buf.data() + self->m_pos);
                    self->m_pos += n;
                  }
                }};
  }

  /**
   * @brief Returns a string view over the characters written so far.
   *
   * @return Non-owning view of the formatted output.
   */
  [[nodiscard]] constexpr std::string_view view() const noexcept {
    return std::string_view(m_buf.data(), m_pos);
  }

  /**
   * @brief Returns the total number of characters written so far.
   *
   * @return Current write offset in bytes.
   */
  [[nodiscard]] constexpr size_t size() const noexcept { return m_pos; }

  /**
   * @brief Returns the remaining unwritten capacity in the buffer.
   *
   * @return Available space in bytes.
   */
  [[nodiscard]] constexpr size_t available() const noexcept {
    return (m_pos < m_buf.size()) ? (m_buf.size() - m_pos) : 0;
  }

  /**
   * @brief Resets the write offset back to the beginning of the buffer.
   */
  constexpr void reset() noexcept { m_pos = 0; }

private:
  span<char> m_buf;
  size_t m_pos{0};
};

/**
 * @brief Output sink that stores formatted characters in an internal fixed-size
 * stack/inline buffer.
 *
 * Provides a self-contained, zero-allocation storage buffer of @p N bytes and
 * exposes a type-erased @ref sink interface. Writes exceeding the buffer
 * capacity are safely truncated.
 *
 * @tparam N The capacity of the internal storage buffer in bytes.
 */
template <std::size_t N> class buffer_sink {
public:
  /**
   * @brief Constructs an empty buffer sink.
   */
  constexpr buffer_sink() noexcept = default;

  /**
   * @brief Creates a type-erased @ref sink adapter pointing to this instance.
   *
   * @return A lightweight @ref sink struct configured with a callback to append
   * to this buffer.
   */
  [[nodiscard]] sink as_sink() noexcept {
    return sink{this, [](void *ctx, std::string_view sv) noexcept {
                  auto *self = static_cast<buffer_sink<N> *>(ctx);
                  const std::size_t avail =
                      (self->m_pos < N) ? (N - self->m_pos) : 0;
                  const std::size_t n = std::min(sv.size(), avail);
                  if (n > 0) {
                    std::copy_n(sv.data(), n, self->m_data + self->m_pos);
                    self->m_pos += n;
                  }
                }};
  }

  /**
   * @brief Returns a string view over the characters written so far.
   *
   * @return Non-owning view of the formatted output.
   */
  [[nodiscard]] constexpr std::string_view view() const noexcept {
    return std::string_view(m_data, m_pos);
  }

  /**
   * @brief Returns the total number of characters written so far.
   *
   * @return Current write offset in bytes.
   */
  [[nodiscard]] constexpr std::size_t size() const noexcept { return m_pos; }

  /**
   * @brief Returns the maximum capacity of the buffer.
   *
   * @return Total capacity in bytes (@p N).
   */
  [[nodiscard]] constexpr std::size_t capacity() const noexcept { return N; }

  /**
   * @brief Returns the remaining unwritten capacity in the buffer.
   *
   * @return Available space in bytes.
   */
  [[nodiscard]] constexpr std::size_t available() const noexcept {
    return (m_pos < N) ? (N - m_pos) : 0;
  }

  /**
   * @brief Returns a @ref microfmt::span view over the written portion of the
   * buffer.
   *
   * @return Span containing the written characters.
   */
  [[nodiscard]] constexpr span<const char> as_span() const noexcept {
    return span<const char>(m_data, m_pos);
  }

#if MICROFMT_HAS_STD_SPAN
  /**
   * @brief Returns a standard `std::span` view over the written portion of the
   * buffer.
   *
   * @return Standard span containing the written characters.
   */
  [[nodiscard]] constexpr std::span<const char> as_std_span() const noexcept {
    return std::span<const char>(m_data, m_pos);
  }
#endif

  /**
   * @brief Resets the write offset back to the beginning of the buffer.
   */
  constexpr void reset() noexcept { m_pos = 0; }

private:
  char m_data[N]{};
  std::size_t m_pos{0};
};

/**
 * @brief Output sink adapter for arbitrary output iterators or raw pointers.
 *
 * Adapts an iterator (e.g., `char*`, `std::back_insert_iterator`) to the
 * type-erased
 * @ref sink interface, forwarding writes using standard algorithm functions.
 *
 * @tparam OutputIt The type of the output iterator.
 */
template <typename OutputIt> class iterator_sink {
public:
  /**
   * @brief Constructs an iterator sink wrapping the target output iterator.
   *
   * @param it Target output iterator.
   */
  explicit constexpr iterator_sink(OutputIt it) noexcept : m_it(it) {}

  /**
   * @brief Creates a type-erased @ref sink adapter pointing to this instance.
   *
   * @return A lightweight @ref sink struct configured with a callback to
   * forward writes.
   */
  [[nodiscard]] sink as_sink() noexcept {
    return sink{this, [](void *ctx, std::string_view sv) noexcept {
                  auto *self = static_cast<iterator_sink<OutputIt> *>(ctx);
                  self->m_it = std::copy(sv.begin(), sv.end(), self->m_it);
                }};
  }

  /**
   * @brief Returns the current state of the output iterator.
   *
   * @return Current output iterator position.
   */
  [[nodiscard]] constexpr OutputIt current() const noexcept { return m_it; }

private:
  OutputIt m_it;
};

/**
 * @brief Output sink that counts total formatted bytes without storing
 * characters.
 *
 * Useful for computing required buffer sizes before performing an allocation or
 * determining truncation lengths without runtime side-effects.
 */
class counting_sink {
public:
  /**
   * @brief Constructs a counting sink with a zero byte count.
   */
  constexpr counting_sink() noexcept = default;

  /**
   * @brief Creates a type-erased @ref sink adapter pointing to this instance.
   *
   * @return A lightweight @ref sink struct configured to increment the internal
   * counter.
   */
  [[nodiscard]] sink as_sink() noexcept {
    return sink{this, [](void *ctx, std::string_view sv) noexcept {
                  auto *self = static_cast<counting_sink *>(ctx);
                  self->m_count += sv.size();
                }};
  }

  /**
   * @brief Returns the total number of bytes written to the sink.
   *
   * @return Total character count.
   */
  [[nodiscard]] constexpr std::size_t count() const noexcept { return m_count; }

  /**
   * @brief Resets the internal counter to zero.
   */
  constexpr void reset() noexcept { m_count = 0; }

private:
  std::size_t m_count{0};
};

/**
 * @brief Null/Discard sink that discards all formatted output.
 *
 * Functions as a zero-cost `/dev/null` sink for benchmarking or conditional
 * output.
 */
class null_sink {
public:
  /**
   * @brief Returns a shared, stateless type-erased @ref sink instance that
   * drops writes.
   *
   * @return A @ref sink struct with a no-op write callback.
   */
  [[nodiscard]] static constexpr sink as_sink() noexcept {
    return sink{nullptr, [](void *, std::string_view) noexcept {}};
  }
};

/**
 * @brief Output sink that guarantees a null-terminated string within a
 * fixed-size buffer.
 *
 * Reserves 1 byte for the trailing `\0`. Output is clamped to `N - 1` bytes and
 * guaranteed to be null-terminated after every write.
 *
 * @tparam N Total buffer size including the null terminator (must be >= 1).
 */
template <std::size_t N> class c_string_sink {
  static_assert(N > 0, "c_string_sink buffer size must be at least 1 byte");

public:
  /**
   * @brief Constructs an empty, null-terminated string sink.
   */
  constexpr c_string_sink() noexcept { m_data[0] = '\0'; }

  /**
   * @brief Creates a type-erased @ref sink adapter pointing to this instance.
   *
   * @return A lightweight @ref sink struct configured to append and
   * null-terminate.
   */
  [[nodiscard]] sink as_sink() noexcept {
    return sink{this, [](void *ctx, std::string_view sv) noexcept {
                  auto *self = static_cast<c_string_sink<N> *>(ctx);
                  constexpr std::size_t max_payload = N - 1;

                  const std::size_t avail = (self->m_pos < max_payload)
                                                ? (max_payload - self->m_pos)
                                                : 0;
                  const std::size_t n = std::min(sv.size(), avail);

                  if (n > 0) {
                    std::copy_n(sv.data(), n, self->m_data + self->m_pos);
                    self->m_pos += n;
                    self->m_data[self->m_pos] = '\0';
                  }
                }};
  }

  /**
   * @brief Returns a null-terminated C string pointer.
   *
   * @return Pointer to the underlying null-terminated buffer.
   */
  [[nodiscard]] constexpr const char *c_str() const noexcept { return m_data; }

  /**
   * @brief Returns a string view over the characters written (excluding null
   * terminator).
   *
   * @return Non-owning view of formatted text.
   */
  [[nodiscard]] constexpr std::string_view view() const noexcept {
    return std::string_view(m_data, m_pos);
  }

  /**
   * @brief Returns the length of the string in bytes (excluding null
   * terminator).
   *
   * @return String length in bytes.
   */
  [[nodiscard]] constexpr std::size_t size() const noexcept { return m_pos; }

  /**
   * @brief Returns the maximum characters the sink can store (excluding null
   * terminator).
   *
   * @return Maximum payload capacity (@p N - 1).
   */
  [[nodiscard]] constexpr std::size_t max_size() const noexcept {
    return N - 1;
  }

  /**
   * @brief Resets the sink to an empty, null-terminated state.
   */
  constexpr void reset() noexcept {
    m_pos = 0;
    m_data[0] = '\0';
  }

private:
  char m_data[N]{};
  std::size_t m_pos{0};
};

/**
 * @brief Output sink that adapts any callable object (lambda, functor) to a
 * @ref sink.
 *
 * @tparam Callable A callable accepting `(std::string_view)` or `(const char*,
 * std::size_t)`.
 */
template <typename Callable> class callback_sink {
public:
  /**
   * @brief Constructs a callback sink wrapping a reference to a callable.
   *
   * @param fn Reference to the target callable.
   */
  explicit constexpr callback_sink(Callable &fn) noexcept : m_fn(&fn) {}

  /**
   * @brief Creates a type-erased @ref sink adapter pointing to the wrapped
   * callable.
   *
   * @return A lightweight @ref sink struct forwarding writes to the callable.
   */
  [[nodiscard]] sink as_sink() noexcept {
    return sink{m_fn, [](void *ctx, std::string_view sv) noexcept {
                  auto *fn = static_cast<Callable *>(ctx);
                  (*fn)(sv);
                }};
  }

private:
  Callable *m_fn;
};

/**
 * @brief Helper factory function to construct a @ref callback_sink with
 * template deduction.
 *
 * @tparam Callable Deduced callable type.
 * @param fn Callable object (e.g., lambda, functor).
 * @return Configured @ref callback_sink instance.
 */
template <typename Callable>
[[nodiscard]] constexpr callback_sink<Callable>
make_callback_sink(Callable &fn) noexcept {
  return callback_sink<Callable>(fn);
}

// ============================================================================
// Formatting Helper Algorithms (Minimal Stack)
// ============================================================================

namespace detail {

inline constexpr char digit_pairs[201] = "00010203040506070809"
                                         "10111213141516171819"
                                         "20212223242526272829"
                                         "30313233343536373839"
                                         "40414243444546474849"
                                         "50515253545556575859"
                                         "60616263646566676869"
                                         "70717273747576777879"
                                         "80818283848586878889"
                                         "90919293949596979899";

#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
inline void format_integer_core(const sink &out, uint64_t val, bool is_negative,
                                uint32_t radix, bool uppercase,
                                int min_width) noexcept {
  char buf[24]; // Reclaimed immediately upon leaf exit
  size_t idx = sizeof(buf);

  if (val == 0) {
    buf[--idx] = '0';
  } else if (radix == 16) {
    const char *hex_digits =
        uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    while (val > 0) {
      buf[--idx] = hex_digits[val & 0xF];
      val >>= 4;
    }
  } else if (radix == 10) {
    while (val >= 100) {
      const auto rem = static_cast<uint32_t>(val % 100);
      val /= 100;
      idx -= 2;
      buf[idx] = digit_pairs[rem * 2];
      buf[idx + 1] = digit_pairs[rem * 2 + 1];
    }
    if (val < 10) {
      buf[--idx] = static_cast<char>('0' + val);
    } else {
      const auto rem = static_cast<uint32_t>(val * 2);
      idx -= 2;
      buf[idx] = digit_pairs[rem];
      buf[idx + 1] = digit_pairs[rem + 1];
    }
  } else {
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    while (val > 0 && idx > 0) {
      buf[--idx] = digits[val % radix];
      val /= radix;
    }
  }

  // Prepend negative sign directly in the buffer if space allows without width
  // padding
  if (is_negative && min_width <= 0) {
    buf[--idx] = '-';
  }

  const size_t digits_len = sizeof(buf) - idx;

  if (min_width > 0) {
    const size_t total_needed = digits_len + (is_negative ? 1 : 0);
    if (is_negative) {
      out.put('-');
    }
    if (static_cast<size_t>(min_width) > total_needed) {
      for (size_t i = 0; i < static_cast<size_t>(min_width) - total_needed;
           ++i) {
        out.put('0');
      }
    }
  }

  out.write(std::string_view(&buf[idx], digits_len));
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
inline void format_unsigned(const sink &out, uint64_t val, uint32_t radix,
                            bool uppercase, int min_width = 0) noexcept {
  format_integer_core(out, val, false, radix, uppercase, min_width);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
inline void format_signed(const sink &out, int64_t val,
                          int min_width = 0) noexcept {
  if (val < 0) {
    // Safe conversion for INT64_MIN (-9223372036854775808)
    const uint64_t mag = static_cast<uint64_t>(-(val + 1)) + 1ULL;
    format_integer_core(out, mag, true, 10, false, min_width);
  } else {
    format_integer_core(out, static_cast<uint64_t>(val), false, 10, false,
                        min_width);
  }
}

} // namespace detail

// ============================================================================
// Format Parse Context & Formatter Customization Point
// ============================================================================

class format_parse_context {
public:
  constexpr explicit format_parse_context(std::string_view spec) noexcept
      : m_spec(spec) {}
  [[nodiscard]] constexpr std::string_view spec() const noexcept {
    return m_spec;
  }
  [[nodiscard]] constexpr bool empty() const noexcept { return m_spec.empty(); }
  [[nodiscard]] constexpr char front() const noexcept {
    return m_spec.empty() ? '\0' : m_spec.front();
  }

private:
  std::string_view m_spec;
};

template <typename T, typename Enable = void> struct formatter;

// ============================================================================
// Built-in Formatter Specializations
// ============================================================================

// Strings (const char*, string_view)
template <> struct formatter<std::string_view> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(std::string_view val, const sink &out) const noexcept {
    out.write(val);
  }
};

template <> struct formatter<const char *> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const char *val, const sink &out) const noexcept {
    out.write(val ? std::string_view(val) : "(null)");
  }
};

// String literal / character array specialization
template <size_t N> struct formatter<char[N]> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const char *val, const sink &out) const noexcept {
    out.write(val ? std::string_view(val) : "(null)");
  }
};

template <size_t N> struct formatter<const char[N]> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const char *val, const sink &out) const noexcept {
    out.write(val ? std::string_view(val) : "(null)");
  }
};

template <> struct formatter<char *> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const char *val, const sink &out) const noexcept {
    out.write(val ? std::string_view(val) : "(null)");
  }
};

// Integers (Signed & Unsigned)
template <typename T>
struct formatter<
    T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> &&
                        !std::is_same_v<T, char>>> {
  char spec{'\0'};
  int width{0};

  constexpr void parse(format_parse_context &ctx) noexcept {
    std::string_view s = ctx.spec();
    if (s.empty())
      return;

    size_t p = 0;
    if (s[p] == '0')
      ++p;
    while (p < s.size() && s[p] >= '0' && s[p] <= '9') {
      width = width * 10 + (s[p] - '0');
      ++p;
    }
    if (p < s.size()) {
      spec = s[p];
    }
  }

  void format(T val, const sink &out) const noexcept {
    if constexpr (std::is_signed_v<T>) {
      if (spec == 'x') {
        detail::format_unsigned(out, static_cast<uint64_t>(val), 16, false,
                                width);
      } else if (spec == 'X') {
        detail::format_unsigned(out, static_cast<uint64_t>(val), 16, true,
                                width);
      } else {
        detail::format_signed(out, static_cast<int64_t>(val), width);
      }
    } else {
      if (spec == 'x') {
        detail::format_unsigned(out, static_cast<uint64_t>(val), 16, false,
                                width);
      } else if (spec == 'X') {
        detail::format_unsigned(out, static_cast<uint64_t>(val), 16, true,
                                width);
      } else {
        detail::format_unsigned(out, static_cast<uint64_t>(val), 10, false,
                                width);
      }
    }
  }
};

// Characters & Booleans
template <> struct formatter<char> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(char val, const sink &out) const noexcept { out.put(val); }
};

template <> struct formatter<bool> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(bool val, const sink &out) const noexcept {
    out.write(val ? "true" : "false");
  }
};

// Raw Pointers
template <typename T>
struct formatter<T *, std::enable_if_t<!std::is_same_v<T, const char> &&
                                       !std::is_same_v<T, char>>> {
  int width{0};

  constexpr void parse(format_parse_context &ctx) noexcept {
    std::string_view s = ctx.spec();
    size_t p = 0;
    if (!s.empty() && s[p] == '0')
      ++p;
    while (p < s.size() && s[p] >= '0' && s[p] <= '9') {
      width = width * 10 + (s[p] - '0');
      ++p;
    }
  }

  void format(T *ptr, const sink &out) const noexcept {
    out.write("0x");
    detail::format_unsigned(out, reinterpret_cast<uintptr_t>(ptr), 16, false,
                            width);
  }
};

template <> struct formatter<std::nullptr_t> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(std::nullptr_t, const sink &out) const noexcept {
    out.write("0x0");
  }
};

// ============================================================================
// Static Trampoline & Type Table Machinery
// ============================================================================

using format_fn_t = void (*)(const void *val_ptr, std::string_view spec,
                             const sink &out) noexcept;

namespace detail {

template <typename T>
inline void format_type_thunk(const void *val_ptr, std::string_view spec,
                              const sink &out) noexcept {
  // std::decay_t converts char[N] -> const char*, float[] -> float*, etc.
  using DecayedT = std::decay_t<T>;

  formatter<DecayedT> f;
  format_parse_context ctx(spec);
  f.parse(ctx);

  if constexpr (std::is_array_v<std::remove_reference_t<T>>) {
    // Arrays are passed by address (const char* pointing to buffer)
    const auto *decayed_val = static_cast<
        const std::remove_all_extents_t<std::remove_reference_t<T>> *>(val_ptr);
    f.format(decayed_val, out);
  } else {
    f.format(*static_cast<const DecayedT *>(val_ptr), out);
  }
}

template <typename... Args> struct format_type_table {
  static constexpr format_fn_t functions[] = {(&format_type_thunk<Args>)...,
                                              nullptr};
};

template <typename... Args>
constexpr format_fn_t format_type_table<Args...>::functions[];

} // namespace detail

// ============================================================================
// Core Execution Loop
// ============================================================================

inline void vformat_to(const sink &out, std::string_view fmt,
                       span<const void *const> arg_ptrs,
                       span<const format_fn_t> arg_fns) noexcept {
  size_t arg_idx = 0;
  size_t i = 0;

  while (i < fmt.size()) {
    const char c = fmt[i];

    if (c == '{') {
      if (i + 1 < fmt.size() && fmt[i + 1] == '{') {
        out.put('{');
        i += 2;
        continue;
      }

      std::string_view spec{};
      size_t close_pos = i + 1;

      while (close_pos < fmt.size() && fmt[close_pos] != '}') {
        if (fmt[close_pos] == ':') {
          spec = fmt.substr(close_pos + 1,
                            fmt.find('}', close_pos) - (close_pos + 1));
        }
        ++close_pos;
      }

      if (close_pos < fmt.size() && fmt[close_pos] == '}') {
        if (arg_idx < arg_ptrs.size() && arg_idx < arg_fns.size()) {
          const void *ptr = arg_ptrs[arg_idx];
          const format_fn_t fn = arg_fns[arg_idx];
          if (fn && ptr) {
            fn(ptr, spec, out);
          }
          ++arg_idx;
        } else {
          out.write("{MISSING}");
        }
        i = close_pos + 1;
        continue;
      }
    } else if (c == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
      out.put('}');
      i += 2;
      continue;
    }

    out.put(c);
    ++i;
  }
}

// ============================================================================
// Public Entry Points
// ============================================================================

template <typename... Args>
inline void format_to(const sink &out, std::string_view fmt,
                      const Args &...args) noexcept {
  if constexpr (sizeof...(Args) == 0) {
    vformat_to(out, fmt, {}, {});
  } else {
    const void *const arg_ptrs[] = {static_cast<const void *>(&args)...};
    constexpr const auto &fns = detail::format_type_table<Args...>::functions;

    vformat_to(out, fmt, span<const void *const>(arg_ptrs, sizeof...(Args)),
               span<const format_fn_t>(fns, sizeof...(Args)));
  }
}

template <typename OutputIt, typename... Args>
inline OutputIt format_to(OutputIt it, std::string_view fmt,
                          const Args &...args) noexcept {
  iterator_sink<OutputIt> isink(it);
  format_to(isink.as_sink(), fmt, args...);
  return isink.current();
}

template <size_t N, typename... Args>
[[nodiscard]] inline buffer_sink<N> format(std::string_view fmt,
                                           const Args &...args) noexcept {
  buffer_sink<N> buf;
  format_to(buf.as_sink(), fmt, args...);
  return buf;
}

} // namespace microfmt
