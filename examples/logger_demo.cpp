// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <cstdint>

#include <microfmt/formatters/hexdump.hpp>
#include <microfmt/log/logger.hpp>

struct sensor_reading {
  uint8_t id;
  int temperature_c;
  uint16_t voltage_mv;
};

template <> struct microfmt::formatter<sensor_reading> {
  constexpr void parse(microfmt::format_parse_context &) noexcept {}

  void format(const sensor_reading &reading,
              const microfmt::sink &out) const noexcept {
    microfmt::format_to(out, "sensor={}, temperature={} C, voltage={} mV",
                        reading.id, reading.temperature_c,
                        reading.voltage_mv);
  }
};

int main() {
  microfmt::log::stdout_color_sink<512> console;
  microfmt::log::basic_logger<1, 512> logger("telemetry", console.as_sink());
  logger.set_level(microfmt::log::level::trace);

  logger.trace("Sampling started");
  logger.info("Reading: {}", sensor_reading{7, 24, 3200});
  logger.warn("Battery voltage is 0x{:04x} mV", 3200);

  const uint8_t packet[] = {0x45, 0x00, 0x00, 0x1c,
                            0x1a, 0x2b, 0x40, 0x00};
  logger.error("Sensor read failed with code {}. Packet:\n{}", 5,
               microfmt::hexdump(
                   microfmt::span<const uint8_t>(packet, sizeof(packet)),
                   0x20001000));
  logger.flush();
}
