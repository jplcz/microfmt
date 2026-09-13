#include <cstdint>

#include <microfmt/formatters/cbor.hpp>
#include <microfmt/formatters/hexdump.hpp>
#include <microfmt/sinks/stdio.hpp>

int main() {
  const uint8_t identifier[] = {0xDE, 0xAD, 0xBE, 0xEF};
  microfmt::buffer_sink<512> encoded;
  {
    microfmt::cbor::map_writer telemetry(encoded.as_sink());
    telemetry.kv("device", "sensor-7")
        .kv("online", true)
        .kv("identifier", microfmt::span(identifier));

    auto readings = telemetry.nested_array("readings");
    readings.val(24).val(25).val(24);
  }

  const auto bytes = microfmt::span<const uint8_t>(
      reinterpret_cast<const uint8_t *>(encoded.view().data()), encoded.size());
  microfmt::println("=== Synthesized CBOR telemetry ===");
  microfmt::println("{}",
                    microfmt::hexdump(bytes, 0, 16, false));
}
