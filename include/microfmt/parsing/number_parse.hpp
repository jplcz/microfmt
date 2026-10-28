#pragma once

/**
 * @file number_parse.hpp
 * @brief Integer parsing engine: Rust-like strict `parse<T>` and `from_chars`-like `parse_prefix<T>`.
 *
 * No heap, no exceptions, no locale, no NUL terminator needed. Works for every built-in integer type,
 * `__int128` where available, and `reloco::fixed_int<N, Signed>` of any width. Floating point lives in the
 * separate opt-in header `number_parse_float.hpp`. Guide: docs/number-parsing.md.
 */

#include "../microfmt.hpp"
#include "../reloco.hpp"
#include <cstddef>
#include <cstdint>
#include <reloco/expected.hpp>
#include <reloco/fixed_int.hpp>
#include <type_traits>

namespace microfmt {

/** @brief Why a number failed to parse. */
enum class number_errc : std::uint8_t {
  empty,         ///< No characters (or only a sign / prefix).
  invalid_digit, ///< A byte that is not a digit of the radix, a misplaced separator, or trailing text.
  overflow,      ///< Above the type's maximum.
  underflow,     ///< Below the type's minimum.
  invalid_radix, ///< Radix not 0 or 2..36.
};

/** @brief Failure code plus the byte offset in the input where parsing failed. */
struct number_error {
  number_errc code{number_errc::empty};
  std::size_t offset{0};
};

/** @brief A successfully parsed value and the number of input bytes it used. */
template <typename T> struct parsed {
  T value{};
  std::size_t consumed{0};
};

/** @brief Integer syntax options. Defaults: decimal, optional '+', no prefix, no separator. */
struct integer_options {
  std::uint8_t radix{10};   ///< 2..36, or 0 = detect from a 0x / 0b / 0o prefix (decimal when absent).
  bool allow_plus{true};    ///< Accept a leading '+'.
  bool allow_prefix{false}; ///< Accept a 0x/0b/0o prefix that matches an explicit radix.
  char separator{'\0'};     ///< Byte allowed between two digits (e.g. '_'); '\0' disables.
};

namespace detail {

template <typename T, typename = void> struct int_traits_width {
  static constexpr std::size_t value = sizeof(T) * 8;
};
template <typename T> struct int_traits_width<T, std::void_t<decltype(T::bit_width)>> {
  static constexpr std::size_t value = T::bit_width;
};

template <typename T> inline constexpr bool is_parsable_int_v =
    (std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>) ||
    std::is_same_v<T, reloco::fixed_int<int_traits_width<T>::value, true>> ||
    std::is_same_v<T, reloco::fixed_int<int_traits_width<T>::value, false>>;

[[nodiscard]] constexpr int digit_value(char ch) noexcept {
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  if (ch >= 'a' && ch <= 'z')
    return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'Z')
    return ch - 'A' + 10;
  return 99;
}

[[nodiscard]] constexpr char byte_at(microfmt::string_view text, std::size_t index) noexcept {
  return index < text.size() ? text.substr(index, 1).front() : '\0';
}

[[nodiscard]] constexpr int prefix_radix(char ch) noexcept {
  switch (ch) {
  case 'x':
  case 'X':
    return 16;
  case 'b':
  case 'B':
    return 2;
  case 'o':
  case 'O':
    return 8;
  default:
    return 0;
  }
}

} // namespace detail

/**
 * @brief Parses the longest valid integer prefix of @p text (like `std::from_chars`).
 * @return the value and consumed byte count, or the error with its offset. Overflow is an error.
 */
template <typename T, typename = std::enable_if_t<detail::is_parsable_int_v<T>>>
[[nodiscard]] constexpr reloco::expected<parsed<T>, number_error>
parse_prefix(microfmt::string_view text, const integer_options &opts = {}) noexcept {
  using U = reloco::fixed_int<detail::int_traits_width<T>::value, false>;
  constexpr bool is_signed_t = static_cast<T>(-1) < static_cast<T>(0);
  using err = reloco::unexpected<number_error>;

  if (opts.radix != 0 && (opts.radix < 2 || opts.radix > 36))
    return err(number_error{number_errc::invalid_radix, 0});

  std::size_t pos = 0;
  bool negative = false;
  const char first = detail::byte_at(text, 0);
  if (first == '-' && is_signed_t) {
    negative = true;
    ++pos;
  } else if (first == '+' && opts.allow_plus) {
    ++pos;
  }

  unsigned radix = opts.radix;
  const int pr = (detail::byte_at(text, pos) == '0') ? detail::prefix_radix(detail::byte_at(text, pos + 1)) : 0;
  if (pr != 0 && (radix == 0 || (opts.allow_prefix && radix == static_cast<unsigned>(pr)))) {
    radix = static_cast<unsigned>(pr);
    pos += 2;
  }
  if (radix == 0)
    radix = 10;

  const U zero = static_cast<U>(0);
  const U max_u = static_cast<U>(~zero);
  const U limit = is_signed_t ? (negative ? static_cast<U>(static_cast<U>(max_u >> 1) + static_cast<U>(1)) : static_cast<U>(max_u >> 1)) : max_u;
  const U radix_u = static_cast<U>(radix);

  U acc = zero;
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
    const int d = detail::digit_value(ch);
    if (d >= static_cast<int>(radix))
      break;
    const U du = static_cast<U>(d);
    if (acc > static_cast<U>(limit / radix_u))
      return err(number_error{negative ? number_errc::underflow : number_errc::overflow, i});
    acc = static_cast<U>(acc * radix_u);
    if (acc > static_cast<U>(limit - du))
      return err(number_error{negative ? number_errc::underflow : number_errc::overflow, i});
    acc = static_cast<U>(acc + du);
    ++count;
    last_sep = false;
    end = i + 1;
  }

  if (count == 0)
    return err(number_error{pos >= text.size() ? number_errc::empty : number_errc::invalid_digit, pos});

  const U bits = negative ? static_cast<U>(zero - acc) : acc;
  return parsed<T>{static_cast<T>(bits), end};
}

/** @brief Parses all of @p text as one integer (like Rust `str::parse`); trailing text is `invalid_digit`. */
template <typename T, typename = std::enable_if_t<detail::is_parsable_int_v<T>>>
[[nodiscard]] constexpr reloco::expected<T, number_error> parse(microfmt::string_view text,
                                                                const integer_options &opts = {}) noexcept {
  auto r = parse_prefix<T>(text, opts);
  if (!r.has_value())
    return reloco::unexpected<number_error>(r.error());
  if (r.value().consumed != text.size())
    return reloco::unexpected<number_error>(number_error{number_errc::invalid_digit, r.value().consumed});
  return r.value().value;
}

} // namespace microfmt
