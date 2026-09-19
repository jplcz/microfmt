// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <microfmt/formatters/floating.hpp>
#include <microfmt/formatters/ranges.hpp>
#include <microfmt/formatters/string.hpp>
#include <microfmt/formatters/variant.hpp>
#include <microfmt/microfmt.hpp>
#include <microfmt/sinks/stdio.hpp>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

// ============================================================================
// Domain Types & Custom Formatters
// ============================================================================

struct HeaderToken {
  std::string key;
  std::string value;
};

struct EndOfStream {
  uint32_t total_bytes;
};

struct GeoLocation {
  double latitude;
  double longitude;
};

namespace microfmt {

template <> struct formatter<HeaderToken> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const HeaderToken &tok, const sink &out) const noexcept {
    microfmt::format_to(out, "{}: {}", tok.key, tok.value);
  }
};

template <> struct formatter<EndOfStream> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const EndOfStream &eos, const sink &out) const noexcept {
    microfmt::format_to(out, "EOF(size={}B)", eos.total_bytes);
  }
};

template <> struct formatter<GeoLocation> {
  microfmt::string_view float_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept { float_spec = ctx.spec(); }

  void format(const GeoLocation &loc, const sink &out) const noexcept {
    if (!float_spec.empty()) {
      microfmt::format_to(out, "Geo({:{}f}, {:{}f})", loc.latitude, float_spec, loc.longitude, float_spec);
    } else {
      microfmt::format_to(out, "Geo({}, {})", loc.latitude, loc.longitude);
    }
  }
};

} // namespace microfmt

// ============================================================================
// Variant Type Definitions
// ============================================================================

// Stream packet tokens
using StreamToken = std::variant<HeaderToken, uint32_t, double, microfmt::string_view, EndOfStream>;

// Sensor readings and telemetry state
using TelemetryValue = std::variant<std::monostate, int32_t, double, microfmt::string_view, GeoLocation>;

// Configuration value model
using ConfigValue = std::variant<bool, int64_t, double, std::string>;

// ============================================================================
// Demo Scenarios
// ============================================================================

void demo_stream_tokens_with_runtime_join() {
  microfmt::println("=================================================");
  microfmt::println(" 1. Stream Tokens with Runtime microfmt::join");
  microfmt::println("=================================================");

  std::vector<StreamToken> stream = {HeaderToken{"Content-Type", "application/octet-stream"},
                                     uint32_t(0xDEADBEEF),
                                     uint32_t(0xCAFE),
                                     3.14159,
                                     microfmt::string_view("PAYLOAD_READY"),
                                     EndOfStream{4096}};

  // Default join
  microfmt::println("Default : [{}]", microfmt::join(stream));

  // Custom runtime delimiter
  microfmt::println("Pipeline: {}", microfmt::join(stream, " | "));

  // Forward format specifier ({:08x}) directly into active variant
  // alternatives
  microfmt::println("Hex Spec: <{:08x}>", microfmt::join(stream, "><"));

  // Iterator subrange join
  microfmt::println("Subrange: [{}]", microfmt::join(stream.begin() + 1, stream.begin() + 4, ", "));
  microfmt::println();
}

void demo_telemetry_batch_with_monostate() {
  microfmt::println("=================================================");
  microfmt::println(" 2. Telemetry Batch with std::monostate");
  microfmt::println("=================================================");

  std::array<TelemetryValue, 6> readings = {std::monostate{},
                                            int32_t(100),
                                            int32_t(255),
                                            23.456,
                                            GeoLocation{52.2297, 21.0122},
                                            microfmt::string_view("SENSOR_OK")};

  // Join array elements
  microfmt::println("Readings: {}", microfmt::join(readings, " -> "));

  // Forwarding radix specifier
  microfmt::println("Hex Mode: {:#x}", microfmt::join(readings, ", "));
  microfmt::println();
}

#if __cplusplus >= 202002L
void demo_compile_time_nttp_join_as() {
  microfmt::println("=================================================");
  microfmt::println(" 3. C++20 Zero-Size NTTP microfmt::join_as");
  microfmt::println("=================================================");

  std::vector<StreamToken> tokens = {uint32_t(0xAA), uint32_t(0xBB), uint32_t(0xCC), microfmt::string_view("SYNC")};

  // Zero-overhead compile-time delimiter
  microfmt::println("NTTP Joined : {}", microfmt::join_as<" // ">(tokens));

  // Compile-time delimiter AND compile-time element specifier "04X"
  microfmt::println("NTTP Hex Spec: [{}]", microfmt::join_as<":", "04X">(tokens));
  microfmt::println();
}
#endif

int main() {
  demo_stream_tokens_with_runtime_join();
  demo_telemetry_batch_with_monostate();

#if __cplusplus >= 202002L
  demo_compile_time_nttp_join_as();
#endif

  return 0;
}

RELOCO_END_UNSAFE_BUFFER_USAGE
