// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file boost_net.hpp @brief Allocation-free Boost.Asio IP formatters. */

#include "../microfmt.hpp"
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/basic_endpoint.hpp>
#include <cstddef>
#include <cstdint>

namespace microfmt {
namespace detail {

inline void format_boost_address_v4(const boost::asio::ip::address_v4 &address,
                                    const sink &out) noexcept {
  const auto bytes = address.to_bytes();
  microfmt::format_to(out, MICROFMT_STRING("{}.{}.{}.{}"), bytes[0], bytes[1],
                      bytes[2], bytes[3]);
}

inline void format_boost_address_v6(const boost::asio::ip::address_v6 &address,
                                    const sink &out) noexcept {
  const auto bytes = address.to_bytes();
  uint16_t words[8]{};
  MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;
  for (std::size_t index = 0; index < 8; ++index) {
    words[index] =
        static_cast<uint16_t>((static_cast<uint16_t>(bytes[index * 2]) << 8) |
                              bytes[index * 2 + 1]);
  }

  std::size_t best_start = 8;
  std::size_t best_length = 0;
  for (std::size_t index = 0; index < 8;) {
    if (words[index] != 0) {
      ++index;
      continue;
    }
    const std::size_t start = index;
    while (index < 8 && words[index] == 0) {
      ++index;
    }
    const std::size_t length = index - start;
    if (length >= 2 && length > best_length) {
      best_start = start;
      best_length = length;
    }
  }

  for (std::size_t index = 0; index < 8;) {
    if (index == best_start) {
      out.write("::");
      index += best_length;
      continue;
    }
    if (index != 0 && index != best_start + best_length) {
      out.put(':');
    }
    detail::format_unsigned(out, words[index], 16, false, 0);
    ++index;
  }
  MICROFMT_END_UNSAFE_BUFFER_USAGE;

  if (address.scope_id() != 0) {
    out.put('%');
    detail::format_unsigned(out, address.scope_id(), 10, false, 0);
  }
}

} // namespace detail

template <> struct formatter<boost::asio::ip::address_v4> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const boost::asio::ip::address_v4 &address,
              const sink &out) const noexcept {
    detail::format_boost_address_v4(address, out);
  }
};

template <> struct formatter<boost::asio::ip::address_v6> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const boost::asio::ip::address_v6 &address,
              const sink &out) const noexcept {
    detail::format_boost_address_v6(address, out);
  }
};

template <> struct formatter<boost::asio::ip::address> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const boost::asio::ip::address &address,
              const sink &out) const noexcept {
    if (address.is_v4()) {
      detail::format_boost_address_v4(address.to_v4(), out);
    } else {
      detail::format_boost_address_v6(address.to_v6(), out);
    }
  }
};

template <typename InternetProtocol>
struct formatter<boost::asio::ip::basic_endpoint<InternetProtocol>> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const boost::asio::ip::basic_endpoint<InternetProtocol> &endpoint,
              const sink &out) const noexcept {
    const auto address = endpoint.address();
    if (address.is_v6()) {
      out.put('[');
      detail::format_boost_address_v6(address.to_v6(), out);
      out.put(']');
    } else {
      detail::format_boost_address_v4(address.to_v4(), out);
    }
    out.put(':');
    detail::format_unsigned(out, endpoint.port(), 10, false, 0);
  }
};

} // namespace microfmt
