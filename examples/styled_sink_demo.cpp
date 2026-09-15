// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdio>

#include <microfmt/microfmt.hpp>
#include <microfmt/sinks/styled_sink.hpp>

int main() {
  microfmt::buffer_sink<256> transformed_output;
  microfmt::transform_sink uppercase(
      transformed_output.as_sink(), microfmt::char_transform::to_upper);
  microfmt::format_to(uppercase.as_sink(), "boot: {} ({} MHz)\n", "ready",
                      480);
  std::printf("transformed: %.*s", static_cast<int>(transformed_output.size()),
              transformed_output.view().data());

  microfmt::buffer_sink<256> prefixed_output;
  microfmt::prefix_sink diagnostic_lines(prefixed_output.as_sink(), "[diag] ");
  microfmt::format_to(diagnostic_lines.as_sink(), "sensor={}\n", 3);
  microfmt::format_to(diagnostic_lines.as_sink(), "voltage={} mV\n", 3080);
  std::printf("prefixed:\n%.*s", static_cast<int>(prefixed_output.size()),
              prefixed_output.view().data());

  microfmt::buffer_sink<256> limited_output;
  microfmt::limit_sink packet_preview(limited_output.as_sink(), 24);
  microfmt::format_to(packet_preview.as_sink(),
                      "packet={} payload={}", 17, "0123456789abcdef");
  std::printf("limited (%zu bytes): %.*s\n", limited_output.size(),
              static_cast<int>(limited_output.size()), limited_output.view().data());
}
