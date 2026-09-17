// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file boost_values.hpp @brief Boost bitset, rational, logic, and integer formatters. */

#include "../microfmt.hpp"
#include <boost/dynamic_bitset.hpp>
#include <boost/logic/tribool.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/rational.hpp>
#include <cstddef>
#include <limits>
#include <type_traits>

namespace microfmt {

template <typename Block, typename Allocator>
struct formatter<boost::dynamic_bitset<Block, Allocator>> {
  unsigned base{2};
  bool uppercase{false};
  bool prefix{false};

  constexpr void parse(format_parse_context &ctx) noexcept {
    for (const char character : ctx.spec()) {
      if (character == 'x' || character == 'X') {
        base = 16;
        uppercase = character == 'X';
      } else if (character == 'b') {
        base = 2;
      } else if (character == '#') {
        prefix = true;
      }
    }
  }

  void format(const boost::dynamic_bitset<Block, Allocator> &bits,
              const sink &out) const noexcept {
    if (prefix) {
      out.write(base == 16 ? (uppercase ? "0X" : "0x") : "0b");
    }
    if (bits.empty()) {
      out.put('0');
      return;
    }

    if (base == 2) {
      for (std::size_t index = bits.size(); index != 0; --index) {
        out.put(bits.test(index - 1) ? '1' : '0');
      }
      return;
    }

    const auto &digits =
        uppercase ? detail::hex_digits_upper : detail::hex_digits_lower;
    const std::size_t nibble_count = (bits.size() + 3) / 4;
    for (std::size_t nibble = nibble_count; nibble != 0; --nibble) {
      unsigned value = 0;
      for (unsigned bit = 0; bit < 4; ++bit) {
        const std::size_t index = (nibble - 1) * 4 + bit;
        if (index < bits.size() && bits.test(index)) {
          value |= 1U << bit;
        }
      }
      out.put(digits[value]);
    }
  }
};

template <typename IntType> struct formatter<boost::rational<IntType>> {
  microfmt::string_view forwarded_spec{};

  constexpr void parse(format_parse_context &ctx) noexcept {
    forwarded_spec = ctx.spec();
  }

  void format(const boost::rational<IntType> &value,
              const sink &out) const noexcept {
    formatter<IntType> integer_formatter;
    format_parse_context integer_context(forwarded_spec);
    integer_formatter.parse(integer_context);
    integer_formatter.format(value.numerator(), out);
    out.put('/');
    integer_formatter.format(value.denominator(), out);
  }
};

template <> struct formatter<boost::logic::tribool> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(boost::logic::tribool value, const sink &out) const noexcept {
    if (boost::logic::indeterminate(value)) {
      out.write("indeterminate");
    } else {
      out.write(value ? "true" : "false");
    }
  }
};

template <unsigned MinBits, unsigned MaxBits,
          boost::multiprecision::cpp_integer_type SignType,
          boost::multiprecision::cpp_int_check_type Checked, typename Allocator,
          boost::multiprecision::expression_template_option ExpressionTemplates>
struct formatter<boost::multiprecision::number<
    boost::multiprecision::cpp_int_backend<MinBits, MaxBits, SignType, Checked,
                                           Allocator>,
    ExpressionTemplates>> {
  bool binary{false};
  bool uppercase{false};
  bool prefix{false};

  constexpr void parse(format_parse_context &ctx) noexcept {
    for (const char character : ctx.spec()) {
      if (character == 'b') {
        binary = true;
      } else if (character == 'X') {
        uppercase = true;
      } else if (character == '#') {
        prefix = true;
      }
    }
  }

  using integer_type = boost::multiprecision::number<
      boost::multiprecision::cpp_int_backend<MinBits, MaxBits, SignType,
                                             Checked, Allocator>,
      ExpressionTemplates>;

  void format(const integer_type &value, const sink &out) const noexcept {
    const auto &backend = value.backend();
    if (backend.sign()) {
      out.put('-');
    }
    if (prefix) {
      out.write(binary ? "0b" : (uppercase ? "0X" : "0x"));
    }

    using limb_type =
        std::remove_cv_t<std::remove_pointer_t<decltype(backend.limbs())>>;
    constexpr unsigned limb_bits = std::numeric_limits<limb_type>::digits;
    const auto *limbs = backend.limbs();
    const std::size_t limb_count = backend.size();
    if (limb_count == 0 ||
        (limb_count == 1 && static_cast<limb_type>(limbs[0]) == 0)) {
      out.put('0');
      return;
    }

    bool started = false;
    MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;
    for (std::size_t limb_index = limb_count; limb_index != 0; --limb_index) {
      const limb_type limb = limbs[limb_index - 1];
      const unsigned digit_count = binary ? limb_bits : limb_bits / 4;
      for (unsigned digit_index = digit_count; digit_index != 0;
           --digit_index) {
        const unsigned shift =
            binary ? digit_index - 1 : (digit_index - 1) * 4;
        const unsigned digit =
            static_cast<unsigned>((limb >> shift) &
                                  static_cast<limb_type>(binary ? 1U : 15U));
        if (!started && digit == 0) {
          continue;
        }
        started = true;
        if (binary) {
          out.put(digit == 0 ? '0' : '1');
        } else {
          out.put((uppercase ? detail::hex_digits_upper
                             : detail::hex_digits_lower)[digit]);
        }
      }
    }
    MICROFMT_END_UNSAFE_BUFFER_USAGE;
    if (!started) {
      out.put('0');
    }
  }
};

} // namespace microfmt
