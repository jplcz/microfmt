// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Example manifest for tools/reflect_dump/reflect_dump_main.cpp. Replace
// this file's contents (or supply your own via -DMICROFMT_REFLECT_DUMP_
// MANIFEST) with #includes for every header that defines a type annotated
// with MICROFMT_REFLECT_FORMAT/MICROFMT_REFLECT_DUMP_ENUM. This example
// mirrors examples/reflection_demo.cpp's types so the two can be compared
// directly: run reflection_demo.cpp for the live -freflection output, and
// this tool for the generated-header equivalent given to a legacy compiler.

#include <cstdint>
#include <microfmt/formatters/reflect_annotate.hpp>

enum class LinkStatus : uint8_t { Disconnected, Connecting, Connected, Fault };
MICROFMT_REFLECT_DUMP_ENUM(LinkStatus);

enum class LogLevel : uint8_t { Debug, Info, Warn, Error };
MICROFMT_REFLECT_DUMP_ENUM(LogLevel);

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
