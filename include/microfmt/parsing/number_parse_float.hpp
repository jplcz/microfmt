#pragma once

/**
 * @file number_parse_float.hpp
 * @brief Opt-in floating-point parsing (`float`, `double`, `long double`) built on the C library's `strto*`.
 *
 * Kept separate from `number_parse.hpp` so code that only needs integers never depends on libc float parsing
 * or the C locale. The syntax is validated here first, so `strto*` extras (leading whitespace, `nan(...)`,
 * hex floats, `inf`) are accepted only when the options allow them. Guide: docs/number-parsing.md.
 */

#include "number_parse.hpp"
#include <cerrno>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <reloco/array.hpp>

// std-interop-file: wraps the hosted C library strto* / localeconv functions

namespace microfmt {

/** @brief Floating-point syntax options. Defaults accept plain decimal numbers only. */
struct float_options {
  bool allow_plus{true};         ///< Accept a leading '+'.
  char separator{'\0'};          ///< Byte allowed between two digits (TOML '_'); '\0' disables.
  bool allow_special{false};     ///< Accept inf / infinity / nan (case-insensitive, optional sign).
  bool allow_hex{false};         ///< Accept C99 hex floats such as 0x1.8p3.
  bool allow_leading_dot{true};  ///< Accept ".5".
  bool allow_trailing_dot{true}; ///< Accept "5.".
  bool overflow_is_error{true};  ///< false: out-of-range values become +/-infinity (Rust behaviour).
};

namespace detail {

/** Longest number text copied for strto* (bytes, excluding the terminating NUL). */
inline constexpr std::size_t float_buffer_capacity = 1023;

[[nodiscard]] constexpr char lower_ascii(char ch) noexcept { return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch; }

[[nodiscard]] constexpr bool word_at(microfmt::string_view text, std::size_t pos, microfmt::string_view word) noexcept {
  for (std::size_t i = 0; i < word.size(); ++i)
    if (lower_ascii(byte_at(text, pos + i)) != byte_at(word, i))
      return false;
  return true;
}

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
// The C strto* functions need a NUL-terminated buffer; callers pass the bounded, terminated local buffer.
[[nodiscard]] inline float strto_t(const char *s, char **end, float) noexcept { return std::strtof(s, end); }
[[nodiscard]] inline double strto_t(const char *s, char **end, double) noexcept { return std::strtod(s, end); }
[[nodiscard]] inline long double strto_t(const char *s, char **end, long double) noexcept { return std::strtold(s, end); }
RELOCO_END_UNSAFE_BUFFER_USAGE

template <typename T> inline constexpr bool is_parsable_float_v =
    std::is_same_v<T, float> || std::is_same_v<T, double> || std::is_same_v<T, long double>;

} // namespace detail

/**
 * @brief Parses the longest valid floating-point prefix of @p text.
 * @return value and consumed bytes, or an error with the offset (overflow when the value is out of range and
 * `overflow_is_error`; underflow to zero/subnormal is not an error).
 */
template <typename T, typename = std::enable_if_t<detail::is_parsable_float_v<T>>>
[[nodiscard]] reloco::expected<parsed<T>, number_error> parse_prefix(microfmt::string_view text,
                                                                     const float_options &opts) noexcept {
  using err = reloco::unexpected<number_error>;
  std::size_t pos = 0;
  bool negative = false;
  const char first = detail::byte_at(text, 0);
  if (first == '-') {
    negative = true;
    ++pos;
  } else if (first == '+' && opts.allow_plus) {
    ++pos;
  }

  if (opts.allow_special) {
    std::size_t len = 0;
    bool is_nan = false;
    if (detail::word_at(text, pos, "infinity"))
      len = 8;
    else if (detail::word_at(text, pos, "inf"))
      len = 3;
    else if (detail::word_at(text, pos, "nan")) {
      len = 3;
      is_nan = true;
    }
    if (len != 0) {
      const T v = is_nan ? std::numeric_limits<T>::quiet_NaN() : std::numeric_limits<T>::infinity();
      return parsed<T>{negative ? -v : v, pos + len};
    }
  }

  const char *point_c = std::localeconv()->decimal_point;
  const microfmt::string_view point = point_c == nullptr ? microfmt::string_view{} : microfmt::string_view(point_c);
  if (point.size() != 1)
    return err(number_error{number_errc::invalid_digit, pos});
  const char locale_point = detail::byte_at(point, 0);

  reloco::array<char, detail::float_buffer_capacity + 1> buf{};
  std::size_t len = 0;
  bool overflowed_buffer = false;
  const auto put = [&](char ch) noexcept {
    if (len >= detail::float_buffer_capacity) {
      overflowed_buffer = true;
      return;
    }
    buf[len++] = ch;
  };
  if (negative)
    put('-');

  const bool hex = opts.allow_hex && detail::byte_at(text, pos) == '0' &&
                   detail::lower_ascii(detail::byte_at(text, pos + 1)) == 'x';
  if (hex) {
    put('0');
    put('x');
    pos += 2;
  }
  const auto is_digit_ch = [&](char ch) noexcept { return hex ? detail::digit_value(ch) < 16 : (ch >= '0' && ch <= '9'); };

  // Digit run with optional separators strictly between digits; returns digit count, advances pos.
  const auto digits = [&]() noexcept {
    std::size_t count = 0;
    std::size_t end = pos;
    bool last_sep = false;
    for (std::size_t i = pos; i < text.size(); ++i) {
      const char ch = detail::byte_at(text, i);
      if (opts.separator != '\0' && ch == opts.separator) {
        if (count == 0 || last_sep)
          break;
        last_sep = true;
        continue;
      }
      if (!is_digit_ch(ch))
        break;
      put(ch);
      ++count;
      last_sep = false;
      end = i + 1;
    }
    pos = end;
    return count;
  };

  const std::size_t int_digits = digits();
  std::size_t frac_digits = 0;
  if (detail::byte_at(text, pos) == '.' && (int_digits != 0 || opts.allow_leading_dot)) {
    // Look ahead: a dot must be followed by a digit, or be an allowed trailing dot after integer digits.
    const bool digit_follows = is_digit_ch(detail::byte_at(text, pos + 1));
    if (digit_follows || (int_digits != 0 && opts.allow_trailing_dot)) {
      ++pos;
      put(locale_point);
      frac_digits = digits();
    }
  }
  if (int_digits + frac_digits == 0)
    return err(number_error{pos >= text.size() ? number_errc::empty : number_errc::invalid_digit, pos});

  const char exp_ch = detail::lower_ascii(detail::byte_at(text, pos));
  if (exp_ch == (hex ? 'p' : 'e')) {
    std::size_t p = pos + 1;
    const char sign = detail::byte_at(text, p);
    const bool has_sign = sign == '+' || sign == '-';
    if (has_sign)
      ++p;
    if (detail::byte_at(text, p) >= '0' && detail::byte_at(text, p) <= '9') {
      put(exp_ch);
      if (has_sign)
        put(sign);
      pos = p;
      for (; detail::byte_at(text, pos) >= '0' && detail::byte_at(text, pos) <= '9'; ++pos)
        put(detail::byte_at(text, pos));
    }
  }

  if (overflowed_buffer)
    return err(number_error{number_errc::invalid_digit, pos});
  buf[len] = '\0';

  errno = 0;
  char *parse_end = nullptr;
  const T value = detail::strto_t(buf.data(), &parse_end, T{});
  const bool range_error = errno == ERANGE;
  if (range_error && std::isinf(value) && opts.overflow_is_error)
    return err(number_error{negative ? number_errc::underflow : number_errc::overflow, pos});
  return parsed<T>{value, pos};
}

/** @brief Parses all of @p text as one floating-point number; trailing text is `invalid_digit`. */
template <typename T, typename = std::enable_if_t<detail::is_parsable_float_v<T>>>
[[nodiscard]] reloco::expected<T, number_error> parse(microfmt::string_view text,
                                                      const float_options &opts) noexcept {
  auto r = parse_prefix<T>(text, opts);
  if (!r.has_value())
    return reloco::unexpected<number_error>(r.error());
  if (r.value().consumed != text.size())
    return reloco::unexpected<number_error>(number_error{number_errc::invalid_digit, r.value().consumed});
  return r.value().value;
}

/** @brief `parse<T>` with default `float_options`. */
template <typename T, typename = std::enable_if_t<detail::is_parsable_float_v<T>>>
[[nodiscard]] reloco::expected<T, number_error> parse(microfmt::string_view text) noexcept {
  return parse<T>(text, float_options{});
}

/** @brief `parse_prefix<T>` with default `float_options`. */
template <typename T, typename = std::enable_if_t<detail::is_parsable_float_v<T>>>
[[nodiscard]] reloco::expected<parsed<T>, number_error> parse_prefix(microfmt::string_view text) noexcept {
  return parse_prefix<T>(text, float_options{});
}

} // namespace microfmt
