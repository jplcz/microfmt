// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

/** @file microfmt.hpp @brief Core formatting primitives, sinks, and
 * customization point. */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "detail/span.hpp"

// Attribute compatibility
#if defined(__has_cpp_attribute) && __has_cpp_attribute(no_unique_address) &&  \
    (__cplusplus >= 202002L)
#define MICROFMT_NO_UNIQUE_ADDRESS [[no_unique_address]]
#elif defined(_MSC_VER) && defined(__has_cpp_attribute) &&                     \
    __has_cpp_attribute(msvc::no_unique_address)
#define MICROFMT_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define MICROFMT_NO_UNIQUE_ADDRESS
#endif

namespace microfmt {

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
  using iterator = std::string_view::const_iterator;
  using const_iterator = std::string_view::const_iterator;
  using value_type = char;
  using size_type = std::size_t;

  constexpr explicit format_parse_context(std::string_view spec) noexcept
      : m_spec(spec) {}

  // --- Core Accessors ---
  [[nodiscard]] constexpr std::string_view spec() const noexcept {
    return m_spec;
  }
  [[nodiscard]] constexpr bool empty() const noexcept { return m_spec.empty(); }
  [[nodiscard]] constexpr size_type size() const noexcept {
    return m_spec.size();
  }

  // --- Iterator Interface (Required for std::format/fmtlib-style custom
  // formatters) ---
  [[nodiscard]] constexpr const_iterator begin() const noexcept {
    return m_spec.begin();
  }
  [[nodiscard]] constexpr const_iterator end() const noexcept {
    return m_spec.end();
  }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
    return m_spec.cbegin();
  }
  [[nodiscard]] constexpr const_iterator cend() const noexcept {
    return m_spec.cend();
  }

  // --- Element Access ---
  [[nodiscard]] constexpr char front() const noexcept {
    return m_spec.empty() ? '\0' : m_spec.front();
  }
  [[nodiscard]] constexpr char back() const noexcept {
    return m_spec.empty() ? '\0' : m_spec.back();
  }
  [[nodiscard]] constexpr char operator[](size_type idx) const noexcept {
    return idx < m_spec.size() ? m_spec[idx] : '\0';
  }

  // --- Parsing & Cursor Advancing Utilities ---
  constexpr void advance_to(const_iterator it) noexcept {
    if (it >= m_spec.begin() && it <= m_spec.end()) {
      m_spec.remove_prefix(static_cast<size_type>(it - m_spec.begin()));
    }
  }

  constexpr void remove_prefix(size_type n) noexcept {
    m_spec.remove_prefix(n < m_spec.size() ? n : m_spec.size());
  }

  constexpr char consume() noexcept {
    if (m_spec.empty())
      return '\0';
    char ch = m_spec.front();
    m_spec.remove_prefix(1);
    return ch;
  }

  [[nodiscard]] constexpr bool starts_with(char ch) const noexcept {
    return !m_spec.empty() && m_spec.front() == ch;
  }

  [[nodiscard]] constexpr bool
  starts_with(std::string_view prefix) const noexcept {
#if __cplusplus >= 202002L
    return m_spec.starts_with(prefix);
#else
    return m_spec.size() >= prefix.size() &&
           m_spec.substr(0, prefix.size()) == prefix;
#endif
  }

  [[nodiscard]] constexpr size_type find(char ch,
                                         size_type pos = 0) const noexcept {
    return m_spec.find(ch, pos);
  }

  [[nodiscard]] constexpr std::string_view
  substr(size_type pos = 0,
         size_type count = std::string_view::npos) const noexcept {
    return m_spec.substr(pos, count);
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

namespace detail {

// 2-digit lookup table for values 00-99
inline constexpr char digits_lut[200] = {
    '0', '0', '0', '1', '0', '2', '0', '3', '0', '4', '0', '5', '0', '6', '0',
    '7', '0', '8', '0', '9', '1', '0', '1', '1', '1', '2', '1', '3', '1', '4',
    '1', '5', '1', '6', '1', '7', '1', '8', '1', '9', '2', '0', '2', '1', '2',
    '2', '2', '3', '2', '4', '2', '5', '2', '6', '2', '7', '2', '8', '2', '9',
    '3', '0', '3', '1', '3', '2', '3', '3', '3', '4', '3', '5', '3', '6', '3',
    '7', '3', '8', '3', '9', '4', '0', '4', '1', '4', '2', '4', '3', '4', '4',
    '4', '5', '4', '6', '4', '7', '4', '8', '4', '9', '5', '0', '5', '1', '5',
    '2', '5', '3', '5', '4', '5', '5', '5', '6', '5', '7', '5', '8', '5', '9',
    '6', '0', '6', '1', '6', '2', '6', '3', '6', '4', '6', '5', '6', '6', '6',
    '7', '6', '8', '6', '9', '7', '0', '7', '1', '7', '2', '7', '3', '7', '4',
    '7', '5', '7', '6', '7', '7', '7', '8', '7', '9', '8', '0', '8', '1', '8',
    '2', '8', '3', '8', '4', '8', '5', '8', '6', '8', '7', '8', '8', '8', '9',
    '9', '0', '9', '1', '9', '2', '9', '3', '9', '4', '9', '5', '9', '6', '9',
    '7', '9', '8', '9', '9'};

inline constexpr char hex_digits_lower[16] = {'0', '1', '2', '3', '4', '5',
                                              '6', '7', '8', '9', 'a', 'b',
                                              'c', 'd', 'e', 'f'};

inline constexpr char hex_digits_upper[16] = {'0', '1', '2', '3', '4', '5',
                                              '6', '7', '8', '9', 'A', 'B',
                                              'C', 'D', 'E', 'F'};

// =============================================================================
// Fast Backward Decimal Formatting
// =============================================================================

template <typename UInt>
#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
inline char *format_dec_backward(char *ptr, UInt value) noexcept {
  // Process 2 digits at a time using the lookup table
  while (value >= 100) {
    const auto rem = static_cast<unsigned>(value % 100);
    value /= 100;
    ptr -= 2;
    ptr[0] = digits_lut[rem * 2];
    ptr[1] = digits_lut[rem * 2 + 1];
  }

  // Handle remaining 1 or 2 digits
  if (value < 10) {
    *--ptr = static_cast<char>('0' + value);
  } else {
    ptr -= 2;
    ptr[0] = digits_lut[value * 2];
    ptr[1] = digits_lut[value * 2 + 1];
  }
  return ptr;
}

// =============================================================================
// Fast Backward Hexadecimal Formatting
// =============================================================================

template <typename UInt>
#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
inline char *format_hex_backward(char *ptr, UInt value,
                                 bool uppercase) noexcept {
  const char *lut = uppercase ? hex_digits_upper : hex_digits_lower;
  if (value == 0) {
    *--ptr = '0';
    return ptr;
  }
  while (value > 0) {
    *--ptr = lut[value & 0xF];
    value >>= 4;
  }
  return ptr;
}

// Low-overhead emission routine with zero-padding and width handling
#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
inline void emit_formatted_int(const sink &out, const char *digits,
                               size_t digits_len, bool is_negative,
                               std::string_view prefix, uint8_t width,
                               bool zero_pad) noexcept {
  const size_t prefix_len = prefix.size();
  const size_t total_content_len =
      digits_len + prefix_len + (is_negative ? 1 : 0);
  const size_t pad_len =
      (width > total_content_len) ? (width - total_content_len) : 0;

  if (zero_pad) {
    // Zero-padded: [sign][prefix][zeros][digits]
    if (is_negative) {
      out.put('-');
    }
    if (!prefix.empty()) {
      out.write(prefix);
    }
    for (size_t i = 0; i < pad_len; ++i) {
      out.put('0');
    }
  } else {
    // Space-padded: [spaces][sign][prefix][digits]
    for (size_t i = 0; i < pad_len; ++i) {
      out.put(' ');
    }
    if (is_negative) {
      out.put('-');
    }
    if (!prefix.empty()) {
      out.write(prefix);
    }
  }

  out.write(std::string_view(digits, digits_len));
}

} // namespace detail

// Integers (Signed & Unsigned)
template <typename T>
struct formatter<
    T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool> &&
                        !std::is_same_v<T, char>>> {
  uint8_t width{0};

  // Bitfield backing all boolean state in a single byte
  // C++17 clean bitfields without in-class initializers
  struct flags_t {
    uint8_t alt_form : 1;
    uint8_t zero_pad : 1;
    uint8_t is_hex : 1;
    uint8_t uppercase : 1;
  } flags{}; // Zero-initializes all bit-field members to 0

  constexpr void parse(format_parse_context &ctx) noexcept {
    std::string_view spec = ctx.spec();
    if (spec.empty())
      return;

    size_t i = 0;

    // Parse '#' (alternate form)
    if (i < spec.size() && spec[i] == '#') {
      flags.alt_form = 1;
      ++i;
    }

    // Parse '0' (zero padding flag)
    if (i < spec.size() && spec[i] == '0') {
      flags.zero_pad = 1;
      ++i;
    }

    // Parse width
    while (i < spec.size() && spec[i] >= '0' && spec[i] <= '9') {
      width = static_cast<uint8_t>(width * 10 + (spec[i] - '0'));
      ++i;
    }

    // Parse type specifier
    if (i < spec.size()) {
      if (spec[i] == 'x') {
        flags.is_hex = 1;
        flags.uppercase = 0;
      } else if (spec[i] == 'X') {
        flags.is_hex = 1;
        flags.uppercase = 1;
      }
    }
  }

  void format(T val, const sink &out) const noexcept {
    constexpr size_t BUF_SIZE = (sizeof(T) <= 4) ? 12 : 24;
    char buffer[BUF_SIZE];
    char *end = buffer + BUF_SIZE;
    char *start = end;

    using unsigned_t = std::make_unsigned_t<T>;
    unsigned_t uval;
    bool is_negative = false;

    if constexpr (std::is_signed_v<T>) {
      if (val < 0) {
        is_negative = true;
        uval = static_cast<unsigned_t>(0) - static_cast<unsigned_t>(val);
      } else {
        uval = static_cast<unsigned_t>(val);
      }
    } else {
      uval = val;
    }

    std::string_view prefix{};
    if (flags.is_hex) {
      start = detail::format_hex_backward(end, uval, flags.uppercase);
      if (flags.alt_form) {
        prefix = flags.uppercase ? "0X" : "0x";
      }
    } else {
      start = detail::format_dec_backward(end, uval);
    }

    const size_t digits_len = static_cast<size_t>(end - start);
    detail::emit_formatted_int(out, start, digits_len, is_negative, prefix,
                               width, flags.zero_pad);
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

#if __cplusplus >= 202002L
template <typename T> using microfmt_remove_cvref_t = std::remove_cvref_t<T>;
#else
template <typename T>
using microfmt_remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;
#endif

// C++17 string_view prefix helper
constexpr bool starts_with(std::string_view sv,
                           std::string_view prefix) noexcept {
  return sv.size() >= prefix.size() &&
         sv.compare(0, prefix.size(), prefix) == 0;
}

constexpr bool starts_with(std::string_view sv, char c) noexcept {
  return !sv.empty() && sv.front() == c;
}

template <typename... Args> struct format_type_table {
  // Static array living in flash / .rodata (Zero runtime RAM usage)
  static inline constexpr std::array<format_fn_t, sizeof...(Args)> functions = {
      &format_type_thunk<microfmt_remove_cvref_t<Args>>...};

  static inline constexpr span<const format_fn_t> dynamic_span{
      functions.data(), functions.size()};
};

// Specialization for zero arguments
template <> struct format_type_table<> {
  static inline constexpr span<const format_fn_t> dynamic_span{};
};

// Represents a pre-parsed action in the format string
struct compiled_piece {
  std::string_view literal{}; // Static literal text to emit directly
  std::string_view spec{};    // Format specifier (e.g. ":08x")
  size_t arg_index{0};        // Which argument index to format
  bool is_arg{false};         // true = format arg, false = write literal
};

template <size_t MaxPieces = 32> struct compiled_format {
  std::array<compiled_piece, MaxPieces> pieces{};
  size_t count{0};
};

template <size_t MaxPieces = 32>
constexpr compiled_format<MaxPieces>
compile_format_string(std::string_view str) noexcept {
  compiled_format<MaxPieces> result{};
  size_t arg_idx = 0;
  size_t lit_start = 0;

  for (size_t i = 0; i < str.size(); ++i) {
    if (str[i] == '{') {
      if (i + 1 < str.size() && str[i + 1] == '{') {
        // Escaped '{{'
        ++i;
        continue;
      }

      // Record preceding literal if non-empty
      if (i > lit_start) {
        result.pieces[result.count++] =
            compiled_piece{str.substr(lit_start, i - lit_start), {}, 0, false};
      }

      // Find matching '}'
      size_t end = i + 1;
      while (end < str.size() && str[end] != '}') {
        ++end;
      }

      const std::string_view replacement =
          str.substr(i + 1, end - (i + 1));
      const size_t colon_pos = replacement.find(':');
      const std::string_view spec =
          colon_pos == std::string_view::npos
              ? std::string_view{}
              : replacement.substr(colon_pos + 1);
      result.pieces[result.count++] = compiled_piece{{}, spec, arg_idx++, true};

      i = end;
      lit_start = i + 1;
    } else if (str[i] == '}') {
      if (i + 1 < str.size() && str[i + 1] == '}') {
        ++i;
        continue;
      }
    }
  }

  // Trailing literal
  if (lit_start < str.size()) {
    result.pieces[result.count++] =
        compiled_piece{str.substr(lit_start), {}, 0, false};
  }

  return result;
}

// Helper to extract the N-th argument from a parameter pack
template <size_t TargetIdx, size_t CurIdx, typename T, typename... Rest>
#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
inline const void *get_arg_by_index(const T &first,
                                    const Rest &...rest) noexcept {
  if constexpr (TargetIdx == CurIdx) {
    return static_cast<const void *>(&first);
  } else {
    return get_arg_by_index<TargetIdx, CurIdx + 1>(rest...);
  }
}

template <typename StrProvider> struct compiled_string_storage {
  static constexpr auto compiled = compile_format_string(StrProvider::get());
};

// Formats a single compiled piece with zero runtime indirect thunks
template <typename StrProvider, size_t PieceIdx, typename... Args>
#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
inline void
emit_piece_by_index(const sink &out,
                    const std::tuple<const Args &...> &arg_tuple) noexcept {
  constexpr auto &piece =
      compiled_string_storage<StrProvider>::compiled.pieces[PieceIdx];

  if constexpr (!piece.is_arg) {
    out.write(piece.literal);
  } else {
    // Select the argument reference from the tuple
    const auto &arg = std::get<piece.arg_index>(arg_tuple);
    using arg_t = microfmt_remove_cvref_t<decltype(arg)>;

    formatter<arg_t> fmt{};
    format_parse_context ctx(piece.spec);
    fmt.parse(ctx);
    fmt.format(arg, out);
  }
}

template <typename StrProvider, typename... Args, size_t... Is>
#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
inline void unrolled_format_impl(const sink &out, std::index_sequence<Is...>,
                                 const Args &...args) noexcept {
  auto arg_tuple = std::forward_as_tuple(args...);
  (void)arg_tuple;
  (emit_piece_by_index<StrProvider, Is>(out, arg_tuple), ...);
}

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
    constexpr auto thunks = detail::format_type_table<Args...>::dynamic_span;

    vformat_to(out, fmt, span<const void *const>(arg_ptrs, sizeof...(Args)),
               thunks);
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

template <typename Provider> struct compile_string_holder {
  using provider_type = Provider;

  [[nodiscard]] constexpr std::string_view get() const noexcept {
    return Provider::get();
  }
};

// Creates a unique static provider for C++17 compatibility
#define MICROFMT_STRING(s)                                                     \
  ([] {                                                                        \
    struct str_provider {                                                      \
      static constexpr std::string_view get() noexcept {                       \
        return std::string_view{s, sizeof(s) - 1};                             \
      }                                                                        \
    };                                                                         \
    return ::microfmt::compile_string_holder<str_provider>{};                  \
  }())

// Compile-time unrolled overload (Zero stack arg_ptrs, zero indirect thunks)
template <typename StrProvider, typename... Args>
inline void format_to(const sink &out, compile_string_holder<StrProvider>,
                      const Args &...args) noexcept {
  constexpr size_t num_pieces =
      detail::compiled_string_storage<StrProvider>::compiled.count;

  detail::unrolled_format_impl<StrProvider>(
      out, std::make_index_sequence<num_pieces>{}, args...);
}

// Compile-time overload
template <typename OutputIt, typename StrProvider, typename... Args>
inline OutputIt format_to(OutputIt it, compile_string_holder<StrProvider> fmt,
                          const Args &...args) noexcept {
  iterator_sink<OutputIt> isink(it);
  format_to(isink.as_sink(), fmt, args...);
  return isink.current();
}

// Compile-time overload
template <size_t N, typename StrProvider, typename... Args>
[[nodiscard]] inline buffer_sink<N>
format(compile_string_holder<StrProvider> fmt, const Args &...args) noexcept {
  buffer_sink<N> buf;
  format_to(buf.as_sink(), fmt, args...);
  return buf;
}

} // namespace microfmt
