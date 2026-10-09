// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <iostream>
#include <string>
#include <vector>

#include <microfmt/sinks/container_sink.hpp>

int main() {
  std::string message = "telemetry: ";
  microfmt::format_to_container(message, "temperature={} C", 24);

  const auto packet = microfmt::format_as_container<std::vector<char>>("id={:04x}, status={}", 0x2a, "ready");

  std::cout << message << "\npacket: " << std::string(packet.begin(), packet.end()) << '\n';
}
