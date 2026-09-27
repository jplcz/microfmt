// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Requires a P2996-capable compiler built with -std=c++26 -freflection
// (GCC 16+ trunk as of this writing); see docs/reflection.md.

#include <cstdint>
#include <cstdio>
#include <microfmt/formatters/reflection.hpp>
#include <microfmt/microfmt.hpp>

// ----------------------------------------------------------------------------
// Enums format automatically -- no opt-in, no per-value macro.
// ----------------------------------------------------------------------------
enum class LinkStatus : uint8_t { Disconnected, Connecting, Connected, Fault };

enum class LogLevel : uint8_t { Debug, Info, Warn, Error };

// ----------------------------------------------------------------------------
// Structs opt in with one line naming the type -- no field list to keep in
// sync, unlike BOOST_DESCRIBE_STRUCT.
// ----------------------------------------------------------------------------
struct SensorReading {
  uint32_t timestamp_ms;
  int32_t temperature_c;
  int32_t pressure_hpa;
};
MICROFMT_REFLECT_FORMAT(SensorReading);

struct NetworkConfig {
  uint32_t ip;
  uint16_t port;
  LinkStatus status;
};
MICROFMT_REFLECT_FORMAT(NetworkConfig);

struct SystemState {
  uint8_t node_id;
  LogLevel verbosity;
  SensorReading sensor;
  NetworkConfig net;
};
MICROFMT_REFLECT_FORMAT(SystemState);

static void terminal_write(void *, microfmt::string_view sv) noexcept {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  std::fwrite(sv.data(), 1, sv.size(), stdout);

  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

int main() {
  microfmt::sink term{nullptr, terminal_write};

  // Enum formatting
  LinkStatus link = LinkStatus::Connected;
  LogLevel lvl = LogLevel::Warn;
  LinkStatus invalid_link = static_cast<LinkStatus>(99);

  microfmt::format_to(term, "=== 1. Enum Formatting ===\n");
  microfmt::format_to(term, "Link Status : {}\n", link);
  microfmt::format_to(term, "Log Level   : {}\n", lvl);
  microfmt::format_to(term, "Invalid Enum: {}\n\n", invalid_link);

  // Struct formatting with nested enum & struct reflection
  SensorReading s{105420, 24, 1013};
  NetworkConfig net{0xC0A80164, 8080, LinkStatus::Connecting};
  SystemState sys{1, LogLevel::Debug, s, net};

  microfmt::format_to(term, "=== 2. Struct Reflection ===\n");
  microfmt::format_to(term, "Sensor: {}\n", s);
  microfmt::format_to(term, "Net   : {}\n", net);
  microfmt::format_to(term, "System: {}\n", sys);

  return 0;
}
