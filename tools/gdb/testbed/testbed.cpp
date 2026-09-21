// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Testbed for microfmt's GDB pretty printers. See tools/gdb/testbed/README.md
// for how to build/run this and how to add a case for a new printer.
//
// Every variable below carries a GDB_CHECK marker comment giving a variable
// name and an expected output substring, separated by "=>". `run.sh`
// extracts those comments, breaks at the GDB_BREAK marker near the end of
// main(), runs a print command for each variable, and checks that the
// printed output contains the expected substring.

#include <microfmt/log/logger.hpp>
#include <microfmt/microfmt.hpp>
#include <microfmt/sinks/c_string_span_sink.hpp>
#include <microfmt/sinks/memory_buffer.hpp>

#include <iterator>
#include <vector>

int main() {
  // -- span_sink -------------------------------------------------------
  char span_storage[32];
  microfmt::span_sink span_sink_val(microfmt::span<char>(span_storage, sizeof(span_storage)));
  microfmt::format_to(span_sink_val.as_sink(), "{}", 7);
  // GDB_CHECK: span_sink_val => written 1 of 32 bytes = "7"

  // -- buffer_sink -------------------------------------------------------
  microfmt::buffer_sink<64> buffer_sink_val;
  microfmt::format_to(buffer_sink_val.as_sink(), "{}", 42);
  // GDB_CHECK: buffer_sink_val => written 2 of 64 bytes = "42"

  // -- c_string_sink -------------------------------------------------------
  microfmt::c_string_sink<16> c_string_sink_val;
  microfmt::format_to(c_string_sink_val.as_sink(), "{}", 99);
  // GDB_CHECK: c_string_sink_val => written 2 of 15 bytes = "99"

  // -- c_string_span_sink ---------------------------------------------------
  char c_span_storage[16];
  microfmt::c_string_span_sink c_string_span_sink_val(microfmt::span<char>(c_span_storage, sizeof(c_span_storage)));
  microfmt::format_to(c_string_span_sink_val.as_sink(), "{}", 321);
  // GDB_CHECK: c_string_span_sink_val => written 3 of 15 bytes = "321"

  // -- counting_sink -------------------------------------------------------
  microfmt::counting_sink counting_sink_val;
  microfmt::format_to(counting_sink_val.as_sink(), "{}", 123);
  // GDB_CHECK: counting_sink_val => counted 3 byte(s)

  // -- iterator_sink -------------------------------------------------------
  std::vector<char> iterator_storage;
  microfmt::iterator_sink<std::back_insert_iterator<std::vector<char>>> iterator_sink_val(
      std::back_inserter(iterator_storage));
  // GDB_CHECK: iterator_sink_val => microfmt::iterator_sink at

  // -- memory_buffer -------------------------------------------------------
  microfmt::memory_buffer<32> memory_buffer_val;
  microfmt::format_to(memory_buffer_val.as_sink(), "{}", 555);
  // GDB_CHECK: memory_buffer_val => length 3, capacity 32 = "555"

  // -- log::basic_logger ----------------------------------------------------
  microfmt::log::logger logger_val{"telemetry"};
  logger_val.set_level(microfmt::log::level::info);
  // GDB_CHECK: logger_val => "telemetry" level=microfmt::log::level::info sinks=0

  // ADD_NEW_CASE_HERE: declare your new type's test variable above this
  // line, with its own `// GDB_CHECK:` comment, before the GDB_BREAK marker.

  int gdb_break_here = 0; // GDB_BREAK
  (void)gdb_break_here;
  return 0;
}
