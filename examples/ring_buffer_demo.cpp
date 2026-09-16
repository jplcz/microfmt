// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <cstdio>
#include <string_view>

#include <microfmt/formatters/ansi.hpp>
#include <microfmt/microfmt.hpp>
#include <microfmt/sinks/ring_buffer_sink.hpp>
#include <microfmt/sinks/stdio.hpp>

// 256-byte circular trace buffer (Power of 2 size)
static microfmt::ring_buffer_sink<256> g_trace_buffer;

// Simulated ISR event recording
void log_sensor_tick(uint32_t tick_ms, uint16_t adc_val, int16_t temp_c) {
  auto trace_sink = g_trace_buffer.as_sink();
  microfmt::format_to(trace_sink, "[{:06d} ms] ADC: 0x{:04x}, Temp: {:d} C\n",
                      tick_ms, adc_val, temp_c);
}

// Simulated emergency HardFault / panic handler
void handle_panic(uint32_t pc, uint32_t lr, const char *reason) {
  // Prominent crash header
  microfmt::println(
      "\n{}", microfmt::ansi::styled(
                  "==================== SYSTEM PANIC ====================",
                  microfmt::ansi::error_style));

  microfmt::println("Reason : {}", microfmt::ansi::red(reason));
  microfmt::println("State  : PC = 0x{:08X}, LR = 0x{:08X}", pc, lr);
  microfmt::println(
      "{}", microfmt::ansi::styled(
                "=================== END CRASH DUMP ===================",
                microfmt::ansi::error_style));

  // Dump chronological logs (oldest to newest)
  microfmt::println(
      "{}", microfmt::ansi::styled("--- RECENT TRACE LOGS (CHRONOLOGICAL) ---",
                                   microfmt::ansi::info_style));

  // Flush circular buffer slices directly to terminal
  g_trace_buffer.dump_to(microfmt::stdout_sink());

  microfmt::println(
      "{}", microfmt::ansi::styled(
                "=================== END CRASH DUMP ===================",
                microfmt::ansi::error_style));
}

int main() {
  microfmt::println("{}",
                    microfmt::ansi::styled("Starting MCU Telemetry System...",
                                           microfmt::ansi::ok_style));

  // Record events that cause the 256-byte ring buffer to wrap around
  for (uint32_t i = 1; i <= 15; ++i) {
    log_sensor_tick(i * 100, static_cast<uint16_t>(0x0200 + i * 8),
                    24 + static_cast<int16_t>(i % 3));
  }

  // Trigger simulated crash
  handle_panic(0x08003A42, 0x080011F0,
               "HARDFAULT: Bus error during DMA transfer");

  return 0;
}