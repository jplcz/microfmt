// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file gdb_encoders.hpp @brief GDB RSP encoders for both Client (requests) and Server (responses) roles. */

#include "../microfmt.hpp"
#include "gdb_packet_metadata.hpp"

namespace microfmt::gdb {

// ============================================================================
// Shared Encoding Helpers
// ============================================================================

namespace detail {
inline void emit_hex_bytes(sink target, span<const uint8_t> data) noexcept {
  static constexpr char hex[] = "0123456789abcdef";
  for (uint8_t b : data) {
    target.put(hex[(b >> 4) & 0x0F]);
    target.put(hex[b & 0x0F]);
  }
}

inline void emit_escaped_binary(sink target, span<const uint8_t> data) noexcept {
  for (uint8_t b : data) {
    if (b == '#' || b == '$' || b == '}' || b == '*') {
      target.put('}');
      target.put(static_cast<char>(b ^ 0x20));
    } else {
      target.put(static_cast<char>(b));
    }
  }
}

inline void write_thread_id(sink target, uint64_t tid) noexcept {
  if (tid == static_cast<uint64_t>(-1)) {
    target.write("-1");
  } else {
    format_to(target, MICROFMT_STRING("{:x}"), tid);
  }
}
} // namespace detail

// ============================================================================
// GDB Client Role: Request Encoder
// ============================================================================

/**
 * @brief Zero-allocation parameters for generating a GDB client request.
 */
struct client_request_view {
  uintptr_t addr{0};
  size_t length{0};
  span<const uint8_t> data{};

  uint64_t thread_id{0};
  char thread_action{'g'};

  uint32_t reg_index{0};
  uint8_t signal{0};

  uint8_t bp_type{0};
  uint32_t bp_kind{0};

  string_view extra_text{};
};

/**
 * @brief Encodes GDB requests (acting as a GDB frontend/client).
 */
class client_request_encoder {
public:
  /**
   * @brief Formats a GDB request payload into a sink using compile-time metadata.
   */
  static void encode(sink target, packet_type type, const client_request_view &view) noexcept {
    const packet_metadata &meta = get_packet_metadata(type);
    if (!meta.prefix.empty()) {
      target.write(meta.prefix);
    }

    switch (type) {
    case packet_type::continue_with_signal:
    case packet_type::step_with_signal:
      format_to(target, MICROFMT_STRING("{:02x}"), view.signal);
      break;

    case packet_type::read_memory:
      format_to(target, MICROFMT_STRING("{:x},{:x}"), view.addr, view.length);
      break;

    case packet_type::write_memory_hex:
      format_to(target, MICROFMT_STRING("{:x},{:x}:"), view.addr, view.length);
      detail::emit_hex_bytes(target, view.data);
      break;

    case packet_type::write_memory_binary:
      format_to(target, MICROFMT_STRING("{:x},{:x}:"), view.addr, view.length);
      detail::emit_escaped_binary(target, view.data);
      break;

    case packet_type::read_single_register:
      format_to(target, MICROFMT_STRING("{:x}"), view.reg_index);
      break;

    case packet_type::write_single_register:
      format_to(target, MICROFMT_STRING("{:x}="), view.reg_index);
      detail::emit_hex_bytes(target, view.data);
      break;

    case packet_type::write_general_registers:
      detail::emit_hex_bytes(target, view.data);
      break;

    case packet_type::insert_break_watch:
    case packet_type::remove_break_watch:
      format_to(target, MICROFMT_STRING("{},{:x},{:x}"), view.bp_type, view.addr, view.bp_kind);
      break;

    case packet_type::set_thread:
      target.put(view.thread_action);
      detail::write_thread_id(target, view.thread_id);
      break;

    case packet_type::thread_is_alive:
      detail::write_thread_id(target, view.thread_id);
      break;

    case packet_type::search_memory:
      format_to(target, MICROFMT_STRING(":{:x};{:x};"), view.addr, view.length);
      detail::emit_hex_bytes(target, view.data);
      break;

    case packet_type::query_supported:
    case packet_type::query_thread_extra_info:
    case packet_type::query_offsets:
    case packet_type::query_symbol:
    case packet_type::v_cont:
    case packet_type::v_attach:
    case packet_type::v_run:
    case packet_type::v_kill:
      if (!view.extra_text.empty()) {
        target.write(type >= packet_type::v_cont ? ";" : ":");
        target.write(view.extra_text);
      }
      break;

    default:
      if (!view.extra_text.empty()) {
        target.write(view.extra_text);
      }
      break;
    }
  }
};

// ============================================================================
// GDB Server Role: Response Encoder
// ============================================================================

enum class server_response_type : uint8_t {
  empty,           // Unsupported/Empty
  ok,              // "OK"
  error,           // "E<errcode>"
  hex_data,        // Memory/register read responses
  binary_data,     // Escaped binary responses
  console_output,  // "O<hex_string>"
  thread_list,     // "m<id1>,<id2>..."
  thread_list_end, // "l"
  stop_signal,     // "T<sig>..."
  stop_exit,       // "W<status>" or "X<sig>"
  raw_string       // Generic strings (e.g., qSupported responses)
};

/**
 * @brief Zero-allocation parameters for generating a GDB server response.
 */
struct server_response_view {
  uint8_t status_code{0}; // Error code, POSIX signal, or exit code
  uint64_t thread_id{0};  // Context thread ID

  span<const uint8_t> data{};        // Payload for hex/binary data
  span<const uint64_t> thread_ids{}; // Active threads

  string_view text{};              // Raw string or console output
  string_view stop_reason_extra{}; // Expedited registers (e.g., "pc:0800;sp:2000;")
};

/**
 * @brief Encodes GDB responses (acting as a GDB server/stub).
 */
class server_response_encoder {
public:
  /**
   * @brief Formats a GDB response payload into a sink.
   */
  static void encode(sink target, server_response_type type, const server_response_view &view) noexcept {
    switch (type) {
    case server_response_type::empty:
      break;

    case server_response_type::ok:
      target.write("OK");
      break;

    case server_response_type::error:
      format_to(target, MICROFMT_STRING("E{:02x}"), view.status_code);
      break;

    case server_response_type::hex_data:
      detail::emit_hex_bytes(target, view.data);
      break;

    case server_response_type::binary_data:
      detail::emit_escaped_binary(target, view.data);
      break;

    case server_response_type::console_output:
      target.put('O');
      detail::emit_hex_bytes(
          target, span<const uint8_t>(reinterpret_cast<const uint8_t *>(view.text.data()), view.text.size()));
      break;

    case server_response_type::thread_list:
      target.put('m');
      for (size_t i = 0; i < view.thread_ids.size(); ++i) {
        if (i > 0)
          target.put(',');
        format_to(target, MICROFMT_STRING("{:x}"), view.thread_ids[i]);
      }
      break;

    case server_response_type::thread_list_end:
      target.put('l');
      break;

    case server_response_type::stop_signal:
      format_to(target, MICROFMT_STRING("T{:02x}"), view.status_code);
      if (view.thread_id != 0) {
        format_to(target, MICROFMT_STRING("thread:{:x};"), view.thread_id);
      }
      if (!view.stop_reason_extra.empty()) {
        target.write(view.stop_reason_extra);
      }
      break;

    case server_response_type::stop_exit:
      format_to(target, MICROFMT_STRING("W{:02x}"), view.status_code);
      break;

    case server_response_type::raw_string:
      target.write(view.text);
      break;
    }
  }
};

} // namespace microfmt::gdb