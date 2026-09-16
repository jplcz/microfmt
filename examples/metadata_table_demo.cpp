// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <microfmt/inspector/concrete_metadata_map.hpp>
#include <microfmt/sinks/stdio.hpp>

int main() {
  microfmt::property_entry entries[6];
  microfmt::concrete_metadata_map metadata(entries);

  const microfmt::string_view device = "EdgeGateway-01";
  const microfmt::string_view firmware = "1.4.2";
  const std::uint32_t build_id = 1842;
  const std::uint32_t uptime_seconds = 86437;
  const int temperature_celsius = 42;
  const bool healthy = true;

  metadata.set("Device", microfmt::value_ref(device));
  metadata.set("Firmware", microfmt::value_ref(firmware));
  metadata.set("Build ID", microfmt::value_ref(build_id));
  metadata.set("Uptime (s)", microfmt::value_ref(uptime_seconds));
  metadata.set("Temperature (C)",
               microfmt::value_ref(temperature_celsius));
  metadata.set("Healthy", microfmt::value_ref(healthy));

  auto output = microfmt::stdout_sink();
  microfmt::md::writer document(output);
  microfmt::concrete_metadata_map::span_iteration_state iteration;
  char value_scratch[64];

  document.h1("Device Diagnostics").newline();
  microfmt::md::write_metadata_table(
      document, metadata.make_view(iteration), value_scratch,
      "Runtime Metadata");

  return 0;
}
