// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Consumes tools/reflect_dump's generated header instead of the live
// microfmt/formatters/reflection.hpp -- deliberately built with an
// *ordinary* compiler (no -freflection, no P2996 support required), to
// demonstrate that the generated formatters work standalone on a legacy
// toolchain. Mirrors examples/reflection_demo.cpp's types, but not its
// output verbatim: enums still render the same way, but structs go
// through a generated reloco::Debug<T> (Rust `#[derive(Debug)]`-style
// "TypeName { field: value, ... }") instead of reflection_demo.cpp's
// live formatter<T> ("{field: value, ...}", no type name).

#include "../example_manifest.hpp"
#include "reflect_dump_demo_generated.hpp"

#include <cstdio>
#include <microfmt/microfmt.hpp>

static void terminal_write(void *, microfmt::string_view sv) noexcept {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  std::fwrite(sv.data(), 1, sv.size(), stdout);

  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

int main() {
  microfmt::sink term{nullptr, terminal_write};

  LinkStatus link = LinkStatus::Connected;
  LogLevel lvl = LogLevel::Warn;
  LinkStatus invalid_link = static_cast<LinkStatus>(99);

  microfmt::format_to(term, "=== 1. Enum Formatting ===\n");
  microfmt::format_to(term, "Link Status : {}\n", link);
  microfmt::format_to(term, "Log Level   : {}\n", lvl);
  microfmt::format_to(term, "Invalid Enum: {}\n\n", invalid_link);

  SensorReading s{105420, 24, 1013};
  NetworkConfig net{0xC0A80164, 8080, LinkStatus::Connecting};
  SystemState sys{1, LogLevel::Debug, s, net};

  microfmt::format_to(term, "=== 2. Struct Reflection (generated) ===\n");
  microfmt::format_to(term, "Sensor: {}\n", s);
  microfmt::format_to(term, "Net   : {}\n", net);
  microfmt::format_to(term, "System: {}\n", sys);

  return 0;
}
