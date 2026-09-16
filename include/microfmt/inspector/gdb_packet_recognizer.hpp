// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file gdb_packet_recognizer.hpp @brief Fast compile-time two-level lookup for GDB RSP packets. */

#include "../microfmt.hpp"
#include "gdb_packet_types.hpp" // Contains packet_type enum
#include <cstddef>
#include <cstdint>

namespace microfmt::gdb {

namespace detail {

/**
 * @brief Level 2: Sub-match rule for multi-character packets.
 */
struct sub_match {
  string_view prefix;
  packet_type type;
};

// ============================================================================
// Level 2 Sub-tables (Must be ordered with longer prefixes first if they overlap)
// ============================================================================

inline constexpr sub_match q_matches[] = {{"qSearch:memory", packet_type::search_memory},
                                          {"qThreadExtraInfo", packet_type::query_thread_extra_info},
                                          {"qSupported", packet_type::query_supported},
                                          {"qfThreadInfo", packet_type::query_first_thread_info},
                                          {"qsThreadInfo", packet_type::query_subsequent_thread},
                                          {"qAttached", packet_type::query_attached},
                                          {"qOffsets", packet_type::query_offsets},
                                          {"qTStatus", packet_type::query_trace_status},
                                          {"qSymbol", packet_type::query_symbol},
                                          {"qCRC", packet_type::query_crc},
                                          {"qC", packet_type::query_current_thread}};

inline constexpr sub_match Q_matches[] = {{"QStartNoAckMode", packet_type::set_start_noack_mode},
                                          {"QProgramSignals", packet_type::set_program_signals},
                                          {"QThreadEvents", packet_type::set_thread_events},
                                          {"QPassSignals", packet_type::set_pass_signals},
                                          {"QNonStop", packet_type::set_non_stop}};

inline constexpr sub_match v_matches[] = {{"vMustReplyEmpty", packet_type::v_must_reply_empty},
                                          {"vCont?", packet_type::v_cont_supported},
                                          {"vAttach", packet_type::v_attach},
                                          {"vStopped", packet_type::v_stopped},
                                          {"vCont", packet_type::v_cont},
                                          {"vKill", packet_type::v_kill},
                                          {"vRun", packet_type::v_run}};

/**
 * @brief Level 1: Primary lookup entry mapped to an ASCII character.
 */
struct primary_match {
  packet_type single_type{packet_type::unknown};
  const sub_match *sub_matches{nullptr};
  size_t sub_count{0};
};

/**
 * @brief Compile-time builder for the 128-entry ASCII primary lookup table.
 */
struct lookup_table_t {
  primary_match entries[128]{};

  constexpr lookup_table_t() noexcept {
    // Direct single-character matches
    entries['?'].single_type = packet_type::halt_reason;
    entries['c'].single_type = packet_type::continue_exec;
    entries['C'].single_type = packet_type::continue_with_signal;
    entries['s'].single_type = packet_type::step_exec;
    entries['S'].single_type = packet_type::step_with_signal;
    entries['k'].single_type = packet_type::kill;
    entries['D'].single_type = packet_type::detach;
    entries['R'].single_type = packet_type::restart;
    entries['!'].single_type = packet_type::enable_extended_mode;

    entries['g'].single_type = packet_type::read_general_registers;
    entries['G'].single_type = packet_type::write_general_registers;
    entries['p'].single_type = packet_type::read_single_register;
    entries['P'].single_type = packet_type::write_single_register;

    entries['m'].single_type = packet_type::read_memory;
    entries['M'].single_type = packet_type::write_memory_hex;
    entries['X'].single_type = packet_type::write_memory_binary;

    entries['Z'].single_type = packet_type::insert_break_watch;
    entries['z'].single_type = packet_type::remove_break_watch;

    entries['H'].single_type = packet_type::set_thread;
    entries['T'].single_type = packet_type::thread_is_alive;
    entries['F'].single_type = packet_type::file_io_request;

    // Map multi-character sub-tables
    entries['q'].sub_matches = q_matches;
    entries['q'].sub_count = sizeof(q_matches) / sizeof(q_matches[0]);

    entries['Q'].sub_matches = Q_matches;
    entries['Q'].sub_count = sizeof(Q_matches) / sizeof(Q_matches[0]);

    entries['v'].sub_matches = v_matches;
    entries['v'].sub_count = sizeof(v_matches) / sizeof(v_matches[0]);
  }
};

// Generate the table entirely at compile time
inline constexpr lookup_table_t gdb_lookup_table{};

} // namespace detail

/**
 * @brief Fast, zero-allocation packet payload recognizer.
 */
class packet_recognizer {
public:
  /**
   * @brief Identifies the high-level packet_type from a raw GDB packet payload.
   *
   * Uses a fast two-level O(1) + O(N) lookup without dynamic memory.
   *
   * @param payload Unescaped packet payload string (without '$' and '#XX').
   * @return Resolved packet_type, or unknown if unrecognized.
   */
  [[nodiscard]] static constexpr packet_type recognize(string_view payload) noexcept {
    if (payload.empty()) {
      return packet_type::unknown;
    }

    // Level 1: O(1) Array Lookup
    const auto first_char = static_cast<unsigned char>(payload[0]);
    if (first_char > 127) {
      return packet_type::unknown;
    }

    MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;
    const auto &entry = detail::gdb_lookup_table.entries[static_cast<size_t>(first_char)];
    MICROFMT_END_UNSAFE_BUFFER_USAGE;

    // Level 2: Sub-table string prefix match
    if (entry.sub_count > 0) {
      for (size_t i = 0; i < entry.sub_count; ++i) {
        MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;
        const auto &sub = entry.sub_matches[i];
        MICROFMT_END_UNSAFE_BUFFER_USAGE;

        // Check if payload starts with the prefix
        if (payload.size() >= sub.prefix.size() && payload.substr(0, sub.prefix.size()) == sub.prefix) {
          return sub.type;
        }
      }
    }

    // Return single character match (or unknown if unmapped)
    return entry.single_type;
  }
};

} // namespace microfmt::gdb