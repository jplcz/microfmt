// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file gdb_packet_metadata.hpp @brief Metadata table for GDB RSP packet types. */

#include "../microfmt.hpp"
#include "gdb_packet_types.hpp"
#include <cstdint>

namespace microfmt::gdb {

/**
 * @brief Metadata descriptor for a GDB RSP packet.
 */
struct MICROFMT_API_CLASS packet_metadata {
  packet_type type;
  string_view name;   // Human-readable identifier for logging/diagnostics
  string_view prefix; // The GDB RSP protocol prefix string (e.g., "qSupported", "m", "Z")
};

/**
 * @brief Compile-time lookup table mapping packet_type to its metadata.
 * Initialized strictly with C++17 positional arguments to ensure compliance.
 */
inline constexpr packet_metadata packet_metadata_table[] = {
    {packet_type::unknown, "unknown", ""},
    {packet_type::halt_reason, "halt_reason", "?"},
    {packet_type::continue_exec, "continue_exec", "c"},
    {packet_type::continue_with_signal, "continue_with_signal", "C"},
    {packet_type::step_exec, "step_exec", "s"},
    {packet_type::step_with_signal, "step_with_signal", "S"},
    {packet_type::kill, "kill", "k"},
    {packet_type::detach, "detach", "D"},
    {packet_type::restart, "restart", "R"},
    {packet_type::enable_extended_mode, "enable_extended_mode", "!"},
    {packet_type::read_general_registers, "read_general_registers", "g"},
    {packet_type::write_general_registers, "write_general_registers", "G"},
    {packet_type::read_single_register, "read_single_register", "p"},
    {packet_type::write_single_register, "write_single_register", "P"},
    {packet_type::read_memory, "read_memory", "m"},
    {packet_type::write_memory_hex, "write_memory_hex", "M"},
    {packet_type::write_memory_binary, "write_memory_binary", "X"},
    {packet_type::search_memory, "search_memory", "qSearch:memory"},
    {packet_type::insert_break_watch, "insert_break_watch", "Z"},
    {packet_type::remove_break_watch, "remove_break_watch", "z"},
    {packet_type::set_thread, "set_thread", "H"},
    {packet_type::thread_is_alive, "thread_is_alive", "T"},
    {packet_type::query_supported, "query_supported", "qSupported"},
    {packet_type::query_first_thread_info, "query_first_thread_info", "qfThreadInfo"},
    {packet_type::query_subsequent_thread, "query_subsequent_thread", "qsThreadInfo"},
    {packet_type::query_current_thread, "query_current_thread", "qC"},
    {packet_type::query_thread_extra_info, "query_thread_extra_info", "qThreadExtraInfo"},
    {packet_type::query_attached, "query_attached", "qAttached"},
    {packet_type::query_offsets, "query_offsets", "qOffsets"},
    {packet_type::query_crc, "query_crc", "qCRC"},
    {packet_type::query_symbol, "query_symbol", "qSymbol"},
    {packet_type::query_trace_status, "query_trace_status", "qTStatus"},
    {packet_type::set_start_noack_mode, "set_start_noack_mode", "QStartNoAckMode"},
    {packet_type::set_pass_signals, "set_pass_signals", "QPassSignals"},
    {packet_type::set_program_signals, "set_program_signals", "QProgramSignals"},
    {packet_type::set_non_stop, "set_non_stop", "QNonStop"},
    {packet_type::set_thread_events, "set_thread_events", "QThreadEvents"},
    {packet_type::v_cont, "v_cont", "vCont"},
    {packet_type::v_cont_supported, "v_cont_supported", "vCont?"},
    {packet_type::v_attach, "v_attach", "vAttach"},
    {packet_type::v_run, "v_run", "vRun"},
    {packet_type::v_kill, "v_kill", "vKill"},
    {packet_type::v_stopped, "v_stopped", "vStopped"},
    {packet_type::v_must_reply_empty, "v_must_reply_empty", "vMustReplyEmpty"},
    {packet_type::file_io_request, "file_io_request", "F"}};

// Guarantee the table matches the enum exactly at compile time
static_assert((sizeof(packet_metadata_table) / sizeof(packet_metadata_table[0])) ==
                  static_cast<size_t>(packet_type::_count),
              "GDB Packet metadata table is missing entries or out of sync with packet_type enum.");

/**
 * @brief Retrieves metadata for a specific packet type with strict bounds checking.
 *
 * @param type The packet_type to query.
 * @return A reference to the metadata descriptor. Falls back to `unknown` if out of bounds.
 */
[[nodiscard]] constexpr const packet_metadata &get_packet_metadata(packet_type type) noexcept {
  const auto index = static_cast<size_t>(type);
  if (index >= static_cast<size_t>(packet_type::_count)) {
    return packet_metadata_table[static_cast<size_t>(packet_type::unknown)];
  }
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  return packet_metadata_table[index];
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

} // namespace microfmt::gdb