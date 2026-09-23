// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file gdb_packet_recognizer.hpp @brief Fast compile-time two-level lookup for GDB RSP packets. */

#include "../microfmt.hpp"
#include "gdb_packet_types.hpp" // Contains packet_type enum
#include <cstddef>
#include <cstdint>

namespace microfmt::gdb {

/**
 * @brief Zero-allocation packet payload recognizer.
 */
class MICROFMT_API_CLASS packet_recognizer {
public:
  struct packet_entry {
    microfmt::string_view prefix;
    packet_type type;

    constexpr bool operator<(const packet_entry &other) const noexcept { return prefix < other.prefix; }
  };

  static constexpr size_t k_count_multichar = []() -> size_t {
    size_t count = 0;
#define __INSPECTOR_GDB_PACKET_TYPE_X(name, str)                                                                       \
  if constexpr (sizeof(str) > 2) {                                                                                     \
    ++count;                                                                                                           \
  }
    INSPECTOR_GDB_PACKET_LIST(__INSPECTOR_GDB_PACKET_TYPE_X)
#undef __INSPECTOR_GDB_PACKET_TYPE_X
    return count;
  }();

  static constexpr auto k_multi_char_table = []() {
    std::array<packet_entry, k_count_multichar> sub{};
    size_t idx = 0;
#define __INSPECTOR_GDB_PACKET_TYPE_X(name, str)                                                                       \
  if constexpr (sizeof(str) > 2) {                                                                                     \
    sub[idx++] = packet_entry{str, packet_type::name};                                                                 \
  }
    INSPECTOR_GDB_PACKET_LIST(__INSPECTOR_GDB_PACKET_TYPE_X)
#undef __INSPECTOR_GDB_PACKET_TYPE_X

    return sub;
  }();

  struct dispatch_entry {
    packet_type type;
    bool is_multi_char;
  };

  static constexpr auto k_first_byte_table = []() {
    std::array<packet_type, 128> table{};
    for (size_t i = 0; i < 128; ++i) {
      table[i] = packet_type::unknown;
    }

#define __INSPECTOR_GDB_PACKET_TYPE_X(name, str)                                                                       \
  if constexpr (sizeof(str) > 2) {                                                                                     \
    unsigned char c = static_cast<unsigned char>((str)[0]);                                                            \
    if (c < 128) {                                                                                                     \
      table[c] = packet_type::_multi_char_marker;                                                                      \
    }                                                                                                                  \
  } else if constexpr (sizeof(str) == 2) {                                                                             \
    unsigned char c = static_cast<unsigned char>((str)[0]);                                                            \
    if (c < 128) {                                                                                                     \
      table[c] = packet_type::name;                                                                                    \
    }                                                                                                                  \
  }
    INSPECTOR_GDB_PACKET_LIST(__INSPECTOR_GDB_PACKET_TYPE_X)
#undef __INSPECTOR_GDB_PACKET_TYPE_X

    return table;
  }();

  /**
   * @brief Identifies the high-level packet_type from a raw GDB packet payload.
   *
   * @param payload Unescaped packet payload string (without '$' and '#XX').
   * @return Resolved packet_type, or unknown if unrecognized.
   */
  [[nodiscard]] static constexpr packet_type recognize(string_view payload) noexcept {
    if (payload.empty()) {
      return packet_type::unknown;
    }

    unsigned char first_char = static_cast<unsigned char>(payload[0]);
    if (first_char >= 128) {
      return packet_type::unknown;
    }

    packet_type match = k_first_byte_table[first_char];

    // Level 1: O(1) Direct match for single-character commands
    if (match != packet_type::_multi_char_marker) {
      return match;
    }

    // Level 2: Linear search over the small unified multi-character table (~23 entries)
    for (const auto &entry : k_multi_char_table) {
      if (payload.substr(0, entry.prefix.size()) == entry.prefix) {
        return entry.type;
      }
    }

    return packet_type::unknown;
  }
};

} // namespace microfmt::gdb