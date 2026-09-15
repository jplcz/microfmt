// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <cstdio>
#include <microfmt/formatters/escaped.hpp>
#include <microfmt/microfmt.hpp>

static void terminal_write(void * /*ctx*/, microfmt::string_view sv) noexcept {
  std::fwrite(sv.data(), 1, sv.size(), stdout);
}

int main() {
  microfmt::sink term{nullptr, terminal_write};

  // ------------------------------------------------------------------------
  // Cellular / AT Command Modem Logging (Handling \r, \n, and Nulls)
  // ------------------------------------------------------------------------
  microfmt::format_to(term, "=== 1. Cellular / AT Command Logging ===\n");

  // Standard AT command with carriage return and line feed
  const microfmt::string_view at_cmd = "AT+CSQ\r\n";
  microfmt::format_to(term, "TX Command  : {}\n", microfmt::escaped(at_cmd));

  // Modem response containing multiple control characters and null padding
  const microfmt::string_view at_response{
      "\r\n+CSQ: 28,99\r\n\r\nOK\r\n\0\0",
      sizeof("\r\n+CSQ: 28,99\r\n\r\nOK\r\n\0\0") - 1};
  microfmt::format_to(term, "RX Response : {}\n",
                      microfmt::escaped(at_response));

  // Command configuration containing inner quotes
  const microfmt::string_view at_apn =
      "AT+CGDCONT=1,\"IP\",\"internet.carrier.com\"\r";
  microfmt::format_to(term, "APN Config  : {}\n\n", microfmt::escaped(at_apn));

  // ------------------------------------------------------------------------
  // Binary Framing & Protocol Packet Inspection
  // ------------------------------------------------------------------------
  microfmt::format_to(term, "=== 2. Binary Packet Parsing & Inspection ===\n");

  // Simulated framed packet: [STX][CMD: 'R'][LEN: 4][PAYLOAD: 0x00, 0x55, 0xAA,
  // 0xFF][ETX][CRC: 0x1B]
  const uint8_t raw_frame[] = {
      0x02,                   // STX (Start of Text)
      'R',  'E',  'A',  'D',  // Command tag (ASCII printable)
      0x00, 0x55, 0xAA, 0xFF, // Binary payload (mixed values)
      0x03,                   // ETX (End of Text)
      0x1B                    // ESC / CRC byte
  };

  // Raw framed packet without outer quotes
  microfmt::format_to(
      term, "Raw Stream  : {}\n",
      microfmt::escaped(microfmt::span(raw_frame), /*quote=*/false));

  // Payload sub-slice inspection with quote wrapper
  microfmt::span<const uint8_t> payload_slice(raw_frame + 5, 4);
  microfmt::format_to(term, "Payload Hex : {}\n\n",
                      microfmt::escaped(payload_slice, /*quote=*/true));

  // ------------------------------------------------------------------------
  // Structured Event Logging into Bounded Stack Buffers
  // ------------------------------------------------------------------------
  microfmt::format_to(term, "=== 3. Format into Fixed Stack Buffer ===\n");

  const char raw_nmea[] =
      "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
  auto log_entry = microfmt::format<256>(
      "[GPS][RX][len={}] raw={}", sizeof(raw_nmea) - 1,
      microfmt::escaped(raw_nmea, sizeof(raw_nmea) - 1, /*quote=*/true));

  std::fwrite(log_entry.view().data(), 1, log_entry.size(), stdout);
  microfmt::format_to(term, "\n");

  return 0;
}
