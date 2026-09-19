// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdio>

#include <microfmt/microfmt.hpp>
#include <microfmt/sinks/tee_sink.hpp>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

int main() {
  microfmt::buffer_sink<32> serial;
  microfmt::buffer_sink<32> trace;
  auto output = microfmt::make_tee(serial.as_sink(), trace.as_sink());

  microfmt::format_to(output.as_sink(), "sensor={}", 42);
  std::printf("serial: %.*s\ntrace:  %.*s\n", static_cast<int>(serial.size()), serial.view().data(),
              static_cast<int>(trace.size()), trace.view().data());
}

RELOCO_END_UNSAFE_BUFFER_USAGE
