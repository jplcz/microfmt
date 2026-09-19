// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>

#include <microfmt/formatters/json.hpp>
#include <microfmt/sinks/stdio.hpp>

int main() {
  microfmt::buffer_sink<512> output;
  {
    microfmt::json::object_writer telemetry(output.as_sink());
    telemetry.kv("device", "sensor-7").kv("online", true);

    {
      auto reading = telemetry.nested_object("reading");
      reading.kv("temperature_c", 24).kv("voltage_mv", 3200);
    }

    {
      auto samples = telemetry.nested_array("samples");
      samples.val(uint8_t{12}).val(uint8_t{15}).val(uint8_t{14});
    }
  }

  microfmt::println("{}", output.view());
  microfmt::println("event={}", microfmt::json::json_obj([](microfmt::json::object_writer &event) {
                      event.as_known().kv("kind", "boot").kv("sequence", 7);
                    }));
}
