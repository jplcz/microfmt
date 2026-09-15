// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <cstdint>
#include <cstdio>
#include <microfmt/formatters/fixed_point.hpp>
#include <microfmt/microfmt.hpp>
#include <microfmt/formatters/ranges.hpp>
#include <string_view>

static void terminal_write(void * /*ctx*/, std::string_view sv) noexcept {
  std::fwrite(sv.data(), 1, sv.size(), stdout);
}

int main() {
  microfmt::sink term{nullptr, terminal_write};

  // ------------------------------------------------------------------------
  // Basic C-Style Arrays & Default vs Custom Delimiters
  // ------------------------------------------------------------------------
  microfmt::format_to(term, "=== 1. Basic Arrays & Delimiters ===\n");

  const int32_t pin_numbers[] = {2, 4, 12, 13, 15};
  microfmt::format_to(term, "Default delimiter : [{}]\n",
                      microfmt::join(pin_numbers));

  const std::string_view breadcrumbs[] = {"sys", "bus", "i2c", "devices",
                                          "0-0048"};
  microfmt::format_to(term, "Custom delimiter  : /{}/\n\n",
                      microfmt::join(breadcrumbs, "/"));

  // ------------------------------------------------------------------------
  // Formatting std::array and Memory Spans
  // ------------------------------------------------------------------------
  microfmt::format_to(term, "=== 2. Spans & Sub-Ranges ===\n");

  std::array<int16_t, 6> dac_samples{0, 512, 1024, 2048, 3072, 4095};

  // Pass entire container
  microfmt::format_to(term, "Full DAC buffer   : {}\n",
                      microfmt::join(dac_samples, " -> "));

  // Sub-span view over memory slice (samples 1 to 4)
  microfmt::span<const int16_t> sample_slice(dac_samples.data() + 1, 3);
  microfmt::format_to(term, "Active slice      : [{}]\n\n",
                      microfmt::join(sample_slice, ", "));

  // ------------------------------------------------------------------------
  // Raw Iterator Pairs
  // ------------------------------------------------------------------------
  microfmt::format_to(term, "=== 3. Iterator Pairs ===\n");

  const char *const sensor_names[] = {"BMP280", "MPU6050", "INA219",
                                      "MAX31865"};
  // Join only first 2 elements using iterator pointers
  microfmt::format_to(term, "Primary sensors   : {}\n\n",
                      microfmt::join(sensor_names, sensor_names + 2, " & "));

  // ------------------------------------------------------------------------
  // Formatting Ranges of Complex / Custom Types
  // ------------------------------------------------------------------------
  microfmt::format_to(term, "=== 4. Ranges of Custom Formatted Views ===\n");

  // Array of zero-float fixed-point voltage readings (millivolts -> volts)
  const microfmt::milli_view<int32_t> voltages[] = {
      microfmt::milli(3305), microfmt::milli(1812), microfmt::milli(1198),
      microfmt::milli(5004)};

  microfmt::format_to(term, "Rail Voltages (V) : [{}]\n\n",
                      microfmt::join(voltages, " | "));

  // ------------------------------------------------------------------------
  // Compile-Time join_as with Custom Element Specifiers
  // ------------------------------------------------------------------------
  microfmt::format_to(term, "=== 5. Compile-Time join_as Specifiers ===\n");

  const uint8_t mac_raw[] = {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E};
  const uint16_t reg_dump[] = {0x00A1, 0x000F, 0x1234, 0xBEEF};

  // Compile-time separator + 2-digit uppercase hex per element
  microfmt::format_to(term, "MAC (join_as)     : {}\n",
                      microfmt::join_as<":", "02X">(mac_raw));

  // Compile-time separator + 4-digit lowercase hex with prefix
  microfmt::format_to(term, "Registers         : [{}]\n",
                      microfmt::join_as<", ", "04x">(reg_dump));

  // Format string specifier forwarding ({:08b} applied to each element)
  const uint8_t flag_masks[] = {0b00000001, 0b00100100, 0b11000000};
  microfmt::format_to(term, "Bitmasks          : {:08b}\n\n",
                      microfmt::join(flag_masks, " | "));

  // ------------------------------------------------------------------------
  // Formatting Directly into Bounded Stack Buffer
  // ------------------------------------------------------------------------
  microfmt::format_to(term, "=== 6. Format into Fixed Stack Buffer ===\n");

  auto msg = microfmt::format<128>("Device MAC: [{}] (Length: {} bytes)",
                                   microfmt::join_as<":", "02X">(mac_raw),
                                   sizeof(mac_raw));

  std::fwrite(msg.view().data(), 1, msg.size(), stdout);
  microfmt::format_to(term, "\n");

  return 0;
}