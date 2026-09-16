// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file gdb_packet_types.hpp @brief Comprehensive enumeration of GDB RSP packet types. */

#include <cstdint>

namespace microfmt::gdb {

/**
 * @brief High-level classification of GDB Remote Serial Protocol commands.
 *
 * Maps raw packet payloads (e.g., 'g', 'm', 'qSupported') to strongly-typed identifiers.
 */
enum class packet_type : uint16_t {
  unknown = 0,

  // ========================================================================
  // Status & Execution Control
  // ========================================================================
  halt_reason,          // '?'     - Indicate the reason the target halted
  continue_exec,        // 'c'     - Continue
  continue_with_signal, // 'C'     - Continue with signal
  step_exec,            // 's'     - Step
  step_with_signal,     // 'S'     - Step with signal
  kill,                 // 'k'     - Kill the target
  detach,               // 'D'     - Detach from the target
  restart,              // 'R'     - Restart the target program
  enable_extended_mode, // '!'     - Enable extended mode

  // ========================================================================
  // Register Access
  // ========================================================================
  read_general_registers,  // 'g'     - Read general registers
  write_general_registers, // 'G'     - Write general registers
  read_single_register,    // 'p'     - Read a single register
  write_single_register,   // 'P'     - Write a single register

  // ========================================================================
  // Memory Access
  // ========================================================================
  read_memory,         // 'm'     - Read memory
  write_memory_hex,    // 'M'     - Write memory (hex encoded)
  write_memory_binary, // 'X'     - Write memory (binary escaped)
  search_memory,       // 'qSearch:memory' - Search memory for a byte sequence

  // ========================================================================
  // Breakpoints and Watchpoints
  // ========================================================================
  insert_break_watch, // 'Z'     - Insert breakpoint/watchpoint (Z0-Z4)
  remove_break_watch, // 'z'     - Remove breakpoint/watchpoint (z0-z4)

  // ========================================================================
  // Thread Operations
  // ========================================================================
  set_thread,      // 'H'     - Set thread for subsequent operations (Hc, Hg)
  thread_is_alive, // 'T'     - Find out if the thread is alive

  // ========================================================================
  // Standard Queries ('q' packets)
  // ========================================================================
  query_supported,         // 'qSupported'      - Query supported features
  query_first_thread_info, // 'qfThreadInfo'    - Get first thread ID in list
  query_subsequent_thread, // 'qsThreadInfo'    - Get subsequent thread ID in list
  query_current_thread,    // 'qC'              - Return current thread ID
  query_thread_extra_info, // 'qThreadExtraInfo'- Get printable string describing a thread
  query_attached,          // 'qAttached'       - Query if target is attached to an existing process
  query_offsets,           // 'qOffsets'        - Query section offsets
  query_crc,               // 'qCRC'            - Compute CRC of memory block
  query_symbol,            // 'qSymbol'         - Symbol lookups
  query_trace_status,      // 'qTStatus'        - Trace experiment status

  // ========================================================================
  // Standard Settings ('Q' packets)
  // ========================================================================
  set_start_noack_mode, // 'QStartNoAckMode' - Disable Ack (+/-) requirement
  set_pass_signals,     // 'QPassSignals'    - Tell target which signals to pass to inferior
  set_program_signals,  // 'QProgramSignals' - Tell target which signals to let the program see
  set_non_stop,         // 'QNonStop'        - Enable/disable non-stop mode
  set_thread_events,    // 'QThreadEvents'   - Enable/disable thread create/death events

  // ========================================================================
  // Extended 'v' Packets
  // ========================================================================
  v_cont,             // 'vCont'           - Extended continue/step actions for specific threads
  v_cont_supported,   // 'vCont?'          - Query supported vCont actions
  v_attach,           // 'vAttach'         - Attach to a process
  v_run,              // 'vRun'            - Run a program
  v_kill,             // 'vKill'           - Kill a specific PID
  v_stopped,          // 'vStopped'        - Acknowledge a stop reply in non-stop mode
  v_must_reply_empty, // 'vMustReplyEmpty' - Dummy packet to test parser

  // ========================================================================
  // File I/O Extension
  // ========================================================================
  file_io_request, // 'F'               - Target requests File I/O from host

  // ------------------------------------------------------------------------
  _count // Helper bound marker, must remain last
};

} // namespace microfmt::gdb
