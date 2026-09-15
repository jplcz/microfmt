// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <cstdio>
#include <microfmt/formatters/hexdump.hpp>
#include <microfmt/markdown.hpp>
#include <microfmt/microfmt.hpp>
#include <string_view>

// Hardware callback example: streams document directly to stdout / UART / SD
// card
static void stream_write(void * /*ctx*/, std::string_view sv) noexcept {
  std::fwrite(sv.data(), 1, sv.size(), stdout);
}

int main() {
  microfmt::sink out_sink{nullptr, stream_write};
  microfmt::md::writer doc(out_sink);

  // ------------------------------------------------------------------------
  // Headings, Text Formatting, and Quotes
  // ------------------------------------------------------------------------
  doc.h1("Firmware Diagnostics Report")
      .println(
          "Generated automatically by **microfmt::md** on bare-metal target.")
      .newline();

  doc.blockquote("Target device: {} (MCU: {}) | Build ID: 0x{:08x}",
                 "EdgeGateway-01", "STM32H743", 0xA1B2C3D4)
      .newline();

  doc.println("Use {}, {}, {}, and {} for readable diagnostics.",
              microfmt::md::bold("bold labels"),
              microfmt::md::italic("context"),
              microfmt::md::strike("obsolete values"),
              microfmt::md::code("register_name"));
  doc.println("Documentation: {}",
              microfmt::md::link("microfmt", "https://github.com/jplcz/microfmt"));
  doc.println("{}",
              microfmt::md::image("microfmt logo", "docs/microfmt-logo.svg"))
      .newline();

  // ------------------------------------------------------------------------
  // Ordered & Unordered Lists (Integer / Millivolt units)
  // ------------------------------------------------------------------------
  doc.h2("System Checks")
      .list_item("Power supply rails: **PASS** (3300 mV rail @ {} mV)", 3295)
      .list_item("External QSPI Flash: **PASS** ({} MB mounted)", 32)
      .list_item("Ethernet PHY: **LINK ACTIVE** ({} Mbps Full-Duplex)", 100)
      .newline();

  doc.h3("Boot Sequence Stages")
      .numbered_item(1, "Initialize vector table and clocks ({} MHz)", 480)
      .numbered_item(2, "Configure peripheral buses and DMA rings")
      .numbered_item(3, "Mount file systems and launch telemetry task")
      .newline();

  doc.h3("Deployment Checklist")
      .task_item(true, "Firmware image signature verified")
      .task_item(true, "Boot configuration persisted")
      .task_item(false, "Confirm telemetry endpoint reachability")
      .nested_list_item(1, "Retry endpoint discovery after {} seconds", 30)
      .newline();

  doc.alert(microfmt::md::admonition::warning,
            [](microfmt::md::writer &w) {
              w.write("> The VCC rail is below its nominal 3300 mV target.\n");
              w.write("> Inspect the regulator before deployment.\n");
            });

  // ------------------------------------------------------------------------
  // Tables with Alignment and Pure Integer Formatting
  // ------------------------------------------------------------------------
  doc.h2("Sensor Telemetry Table");

  const microfmt::md::column columns[] = {
      {"Sensor", 14, microfmt::md::align::left},
      {"Channel", 9, microfmt::md::align::center},
      {"Reading", 12, microfmt::md::align::right},
      {"Status", 8, microfmt::md::align::center}};

  doc.table_header(microfmt::span(columns));

  // Row 1: Temperature (Integer Celsius)
  doc.table_row_begin()
      .table_cell("Core Temp", columns[0])
      .table_cell_fmt(columns[1], "#{}", 1)
      .table_cell_fmt(columns[2], "{} C", 42)
      .table_cell("OK", columns[3])
      .table_row_end();

  // Row 2: Bus Voltage (mV instead of float Volts)
  doc.table_row_begin()
      .table_cell("Bus Voltage", columns[0])
      .table_cell_fmt(columns[1], "#{}", 2)
      .table_cell_fmt(columns[2], "{} mV", 24100)
      .table_cell("OK", columns[3])
      .table_row_end();

  // Row 3: VCC Rail (mV)
  doc.table_row_begin()
      .table_cell("VCC Rail", columns[0])
      .table_cell_fmt(columns[1], "#{}", 3)
      .table_cell_fmt(columns[2], "{} mV", 3080)
      .table_cell("WARN", columns[3])
      .table_row_end();

  doc.newline().horizontal_rule().newline();

  // ------------------------------------------------------------------------
  // Collapsible Hexdump Block
  // ------------------------------------------------------------------------
  doc.h2("Crash Context Payload");

  const uint8_t crash_dump[] = {0x50, 0x41, 0x4e, 0x49, 0x43, 0x21, 0x00, 0x00,
                                0xef, 0xbe, 0xad, 0xde, 0x01, 0x02, 0x03, 0x04,
                                0x48, 0x61, 0x72, 0x64, 0x46, 0x61, 0x75, 0x6c,
                                0x74, 0x5f, 0x49, 0x53, 0x52};

  doc.hexdump_block(
      "Expand the captured HardFault payload",
      microfmt::span<const uint8_t>(crash_dump, sizeof(crash_dump)));

  return 0;
}