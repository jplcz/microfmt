// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file gdb_decoders.hpp
 * @brief GDB RSP client-request and server-response decoders. */

#include "../microfmt.hpp"
#include "../scratch_allocator.hpp"
#include "../reloco.hpp"
#include "gdb_encoders.hpp"
#include "gdb_packet_recognizer.hpp"

namespace microfmt::gdb {

namespace detail {
[[nodiscard]] constexpr bool parse_hex_nibble(char c, uint8_t &val) noexcept {
  if (c >= '0' && c <= '9') {
    val = static_cast<uint8_t>(c - '0');
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    val = static_cast<uint8_t>(c - 'a' + 10);
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    val = static_cast<uint8_t>(c - 'A' + 10);
    return true;
  }
  return false;
}

[[nodiscard]] constexpr uintptr_t parse_hex_int(string_view hex, size_t *chars_consumed = nullptr) noexcept {
  uintptr_t val = 0;
  size_t count = 0;
  for (char c : hex) {
    uint8_t n = 0;
    if (!parse_hex_nibble(c, n))
      break;
    val = (val << 4) | n;
    count++;
  }
  if (chars_consumed)
    *chars_consumed = count;
  return val;
}

[[nodiscard]] constexpr uint64_t parse_thread_id(string_view hex) noexcept {
  if (hex == "-1")
    return static_cast<uint64_t>(-1);
  return static_cast<uint64_t>(parse_hex_int(hex));
}
} // namespace detail

// ============================================================================
// GDB Server Role: Decoding Client Requests
// ============================================================================

/**
 * @brief Parses incoming GDB client requests (acting as a GDB server).
 */
class MICROFMT_API_CLASS client_request_decoder {
public:
  /**
   * @brief Decodes a raw payload into a structured request view.
   *
   * @param payload The raw string payload (without '$' or '#XX').
   * @param scratch Scratch allocator for decoding hex/binary data payloads.
   * @param out_view Populated with the decoded arguments.
   * @return The identified packet_type, or unknown on failure/unrecognized.
   */
  static packet_type decode(string_view payload, scratch_allocator &scratch, client_request_view &out_view) noexcept {
    out_view = {}; // Reset view
    packet_type type = packet_recognizer::recognize(payload);
    if (type == packet_type::unknown)
      return type;

    const packet_metadata &meta = get_packet_metadata(type);
    string_view args = payload;
    if (payload.starts_with(meta.prefix)) {
      args = payload.substr(meta.prefix.size());
    }

    switch (type) {
    case packet_type::continue_with_signal:
    case packet_type::step_with_signal:
      out_view.signal = static_cast<uint8_t>(detail::parse_hex_int(args));
      break;

    case packet_type::read_memory: {
      size_t consumed = 0;
      out_view.addr = detail::parse_hex_int(args, &consumed);
      if (consumed < args.size() && args[consumed] == ',') {
        out_view.length = static_cast<size_t>(detail::parse_hex_int(args.substr(consumed + 1)));
      }
      break;
    }

    case packet_type::write_memory_hex:
    case packet_type::write_memory_binary:
    case packet_type::search_memory: {
      // Format: <addr>,<length>:<data>  (or :addr;len;data for search)
      if (type == packet_type::search_memory && args.starts_with(":")) {
        args = args.substr(1);
      }
      size_t sep1 = args.find(type == packet_type::search_memory ? ';' : ',');
      size_t sep2 = args.find(type == packet_type::search_memory ? ';' : ':', sep1 != string_view::npos ? sep1 + 1 : 0);

      if (sep1 != string_view::npos && sep2 != string_view::npos) {
        out_view.addr = detail::parse_hex_int(args.substr(0, sep1));
        out_view.length = static_cast<size_t>(detail::parse_hex_int(args.substr(sep1 + 1, sep2 - sep1 - 1)));

        string_view data_str = args.substr(sep2 + 1);
        if (type == packet_type::write_memory_binary) {
          out_view.data = decode_escaped_binary(data_str, scratch);
        } else {
          out_view.data = decode_hex_bytes(data_str, scratch);
        }
      }
      break;
    }

    case packet_type::read_single_register:
      out_view.reg_index = static_cast<uint32_t>(detail::parse_hex_int(args));
      break;

    case packet_type::write_single_register: {
      size_t eq = args.find('=');
      if (eq != string_view::npos) {
        out_view.reg_index = static_cast<uint32_t>(detail::parse_hex_int(args.substr(0, eq)));
        out_view.data = decode_hex_bytes(args.substr(eq + 1), scratch);
      }
      break;
    }

    case packet_type::write_general_registers:
      out_view.data = decode_hex_bytes(args, scratch);
      break;

    case packet_type::insert_break_watch:
    case packet_type::remove_break_watch: {
      if (args.size() > 0)
        out_view.bp_type = static_cast<uint8_t>(args[0] - '0');
      size_t c1 = args.find(',');
      size_t c2 = args.find(',', c1 != string_view::npos ? c1 + 1 : 0);
      if (c1 != string_view::npos && c2 != string_view::npos) {
        out_view.addr = detail::parse_hex_int(args.substr(c1 + 1, c2 - c1 - 1));
        out_view.bp_kind = static_cast<uint32_t>(detail::parse_hex_int(args.substr(c2 + 1)));
      }
      break;
    }

    case packet_type::set_thread:
      if (args.size() > 0) {
        out_view.thread_action = args[0];
        out_view.thread_id = detail::parse_thread_id(args.substr(1));
      }
      break;

    case packet_type::thread_is_alive:
      out_view.thread_id = detail::parse_thread_id(args);
      break;

    default:
      // Pass complex or unparsed fields (like vCont) natively
      out_view.extra_text = args;
      break;
    }
    return type;
  }

private:
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  static span<const uint8_t> decode_hex_bytes(string_view hex, scratch_allocator &scratch) noexcept {
    if ((hex.size() % 2) != 0) {
      return {};
    }
    const size_t len = hex.size() / 2;

    uint8_t *dest = scratch.allocate<uint8_t>(len);
    if (!dest) {
      return {}; // Scratch memory exhausted
    }

    for (size_t i = 0; i < len; ++i) {
      uint8_t h = 0, l = 0;
      if (!detail::parse_hex_nibble(hex[i * 2], h) || !detail::parse_hex_nibble(hex[i * 2 + 1], l)) {
        return {};
      }
      dest[i] = static_cast<uint8_t>((h << 4) | l);
    }

    return {dest, len};
  }

  static span<const uint8_t> decode_escaped_binary(string_view bin, scratch_allocator &scratch) noexcept {
    // Worst-case size is identical to string_view length. We allocate the max bound.
    uint8_t *dest = scratch.allocate<uint8_t>(bin.size());
    if (!dest) {
      return {}; // Scratch memory exhausted
    }

    size_t written = 0;
    bool escaped = false;

    for (char c : bin) {
      if (escaped) {
        dest[written++] = static_cast<uint8_t>(c ^ 0x20);
        escaped = false;
      } else if (c == '}') {
        escaped = true;
      } else {
        dest[written++] = static_cast<uint8_t>(c);
      }
    }

    // Return a precisely sized span over the decoded segment
    return {dest, written};
  }
  RELOCO_END_UNSAFE_BUFFER_USAGE
};

// ============================================================================
// GDB Client Role: Decoding Server Responses
// ============================================================================

/**
 * @brief Parses incoming GDB server responses (acting as a GDB client).
 */
class MICROFMT_API_CLASS server_response_decoder {
public:
  static server_response_type decode(string_view payload, server_response_view &out_view) noexcept {
    out_view = {};

    if (payload.empty())
      return server_response_type::empty;
    if (payload == "OK")
      return server_response_type::ok;
    if (payload == "l")
      return server_response_type::thread_list_end;

    char prefix = payload[0];

    if (prefix == 'E' && payload.size() >= 3) {
      out_view.status_code = static_cast<uint8_t>(detail::parse_hex_int(payload.substr(1, 2)));
      return server_response_type::error;
    }

    if (prefix == 'T' && payload.size() >= 3) {
      out_view.status_code = static_cast<uint8_t>(detail::parse_hex_int(payload.substr(1, 2)));
      out_view.stop_reason_extra = payload.substr(3);
      return server_response_type::stop_signal;
    }

    if (prefix == 'W' || prefix == 'X') {
      out_view.status_code = static_cast<uint8_t>(detail::parse_hex_int(payload.substr(1)));
      return server_response_type::stop_exit;
    }

    if (prefix == 'O') {
      out_view.text = payload.substr(1);
      return server_response_type::console_output;
    }

    if (prefix == 'm' && payload.find(',') != string_view::npos) {
      out_view.text = payload.substr(1);
      return server_response_type::thread_list;
    }

    out_view.text = payload;
    return server_response_type::raw_string;
  }

  /**
   * @brief Utility for the client to decode a `raw_string` hex response (e.g., memory or register blocks)
   * directly into the scratch allocator.
   */
  static span<const uint8_t> decode_hex_data(string_view hex_data, scratch_allocator &scratch) noexcept {
    if ((hex_data.size() % 2) != 0) {
      return {};
    }
    const size_t len = hex_data.size() / 2;

    uint8_t *dest = scratch.allocate<uint8_t>(len);
    if (!dest) {
      return {}; // Scratch memory exhausted
    }

    for (size_t i = 0; i < len; ++i) {
      uint8_t h = 0, l = 0;
      if (!detail::parse_hex_nibble(hex_data[i * 2], h) || !detail::parse_hex_nibble(hex_data[i * 2 + 1], l)) {
        return {};
      }
      RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
      dest[i] = static_cast<uint8_t>((h << 4) | l);
      RELOCO_END_UNSAFE_BUFFER_USAGE;
    }

    return {dest, len};
  }
};

} // namespace microfmt::gdb