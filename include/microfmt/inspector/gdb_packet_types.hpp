// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file gdb_packet_types.hpp @brief Comprehensive enumeration of GDB RSP packet types. */

#include <cstdint>

namespace microfmt::gdb {

#define INSPECTOR_GDB_PACKET_LIST(X)                                                                                   \
  X(unknown, "")                                                                                                       \
  X(enable_extended_mode, "!")                                                                                         \
  X(halt_reason, "?")                                                                                                  \
  X(continue_exec, "c")                                                                                                \
  X(continue_with_signal, "C")                                                                                         \
  X(step_exec, "s")                                                                                                    \
  X(step_with_signal, "S")                                                                                             \
  X(kill, "k")                                                                                                         \
  X(detach, "D")                                                                                                       \
  X(restart, "R")                                                                                                      \
  X(read_general_registers, "g")                                                                                       \
  X(write_general_registers, "G")                                                                                      \
  X(read_single_register, "p")                                                                                         \
  X(write_single_register, "P")                                                                                        \
  X(read_memory, "m")                                                                                                  \
  X(write_memory_hex, "M")                                                                                             \
  X(write_memory_binary, "X")                                                                                          \
  X(search_memory, "qSearch:memory")                                                                                   \
  X(insert_break_watch, "Z")                                                                                           \
  X(remove_break_watch, "z")                                                                                           \
  X(set_thread, "H")                                                                                                   \
  X(thread_is_alive, "T")                                                                                              \
  X(query_supported, "qSupported")                                                                                     \
  X(query_first_thread_info, "qfThreadInfo")                                                                           \
  X(query_subsequent_thread, "qsThreadInfo")                                                                           \
  X(query_current_thread, "qC")                                                                                        \
  X(query_thread_extra_info, "qThreadExtraInfo")                                                                       \
  X(query_attached, "qAttached")                                                                                       \
  X(query_offsets, "qOffsets")                                                                                         \
  X(query_crc, "qCRC")                                                                                                 \
  X(query_symbol, "qSymbol")                                                                                           \
  X(query_trace_status, "qTStatus")                                                                                    \
  X(set_start_noack_mode, "QStartNoAckMode")                                                                           \
  X(set_pass_signals, "QPassSignals")                                                                                  \
  X(set_program_signals, "QProgramSignals")                                                                            \
  X(set_non_stop, "QNonStop")                                                                                          \
  X(set_thread_events, "QThreadEvents")                                                                                \
  X(v_cont, "vCont")                                                                                                   \
  X(v_cont_supported, "vCont?")                                                                                        \
  X(v_attach, "vAttach")                                                                                               \
  X(v_run, "vRun")                                                                                                     \
  X(v_kill, "vKill")                                                                                                   \
  X(v_stopped, "vStopped")                                                                                             \
  X(v_must_reply_empty, "vMustReplyEmpty")                                                                             \
  X(file_io_request, "F")

/**
 * @brief High-level classification of GDB Remote Serial Protocol commands.
 *
 * Maps raw packet payloads (e.g., 'g', 'm', 'qSupported') to strongly-typed identifiers.
 */
enum class packet_type : uint8_t {
#undef __INSPECTOR_GDB_PACKET_TYPE_X
#define __INSPECTOR_GDB_PACKET_TYPE_X(name, str) name,
  INSPECTOR_GDB_PACKET_LIST(__INSPECTOR_GDB_PACKET_TYPE_X)
#undef __INSPECTOR_GDB_PACKET_TYPE_X
      _count,         // Number of packets
  _multi_char_marker, // Special internal sentinel for lookup dispatch
};

} // namespace microfmt::gdb
