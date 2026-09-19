// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Demonstrates the zero-allocation Boost.Asio sink adapters: formatting
// directly into a `boost::asio::mutable_buffer` (e.g. a stack buffer ready
// for `boost::asio::write`/`async_write`), and into a growable
// `boost::asio::streambuf` for larger or variable-length payloads.

#include <cstdio>

#include <boost/asio/buffer.hpp>
#include <boost/asio/streambuf.hpp>

#include <microfmt/sinks/asio_sink.hpp>

int main() {
  // --- Fixed-size stack buffer, ready for a single write()/async_write() ---
  char stack_buffer[64];
  microfmt::asio_mutable_buffer_sink buffer_sink(boost::asio::buffer(stack_buffer));
  microfmt::format_to(buffer_sink.as_sink(), "PING id={:04x} seq={}", 0x2a, 7);

  const auto written = buffer_sink.written_buffer();
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  std::printf("mutable_buffer: %.*s (bytes=%zu)\n", static_cast<int>(written.size()),
              static_cast<const char *>(written.data()), buffer_sink.size());

  RELOCO_END_UNSAFE_BUFFER_USAGE;

  // --- Growable streambuf, useful when the payload size is not known up front ---
  boost::asio::streambuf dynamic_buffer;
  microfmt::asio_streambuf_sink stream_sink(dynamic_buffer, /*max_chars=*/256);
  microfmt::format_to(stream_sink.as_sink(), "telemetry: temperature={} C, humidity={}%", 24, 55);

  const auto data = dynamic_buffer.data();

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  std::printf("streambuf: %.*s\n", static_cast<int>(data.size()), static_cast<const char *>(data.data()));

  RELOCO_END_UNSAFE_BUFFER_USAGE;
}
