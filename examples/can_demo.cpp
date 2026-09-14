// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <cstdint>

#include <microfmt/formatters/can.hpp>
#include <microfmt/sinks/stdio.hpp>

int main() {
  const uint8_t engine_status[] = {0x10, 0x7D, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00};
  const uint8_t diagnostic_request[] = {0x02, 0x01, 0x0C, 0x00,
                                        0x00, 0x00, 0x00, 0x00};
  const uint8_t firmware_chunk[] = {0xDE, 0xAD, 0xBE, 0xEF,
                                    0x01, 0x02, 0x03, 0x04};

  const auto status = microfmt::can_frame(0x123, engine_status);
  const auto request = microfmt::can_extended(
      0x18DAF110, microfmt::span(diagnostic_request));
  const auto update = microfmt::can_fd(
      0x1CEBFF01, microfmt::span(firmware_chunk), true, true);

  microfmt::println("=== Synthesized CAN traffic ===");
  microfmt::println("{}", status);
  microfmt::println("{}", request);
  microfmt::println("{}", update);
  microfmt::println("\n=== candump output ===");
  microfmt::println("can0  {:c}", status);
  microfmt::println("can0  {:c}", request);
  microfmt::println("can0  {:c}", update);
}
