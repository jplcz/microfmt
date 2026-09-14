// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <array>
#include <cstddef>
#include <cstdio>
#include <memory_resource>

#include <microfmt/sinks/pmr_sink.hpp>

int main() {
  std::array<std::byte, 512> storage{};
  std::pmr::monotonic_buffer_resource resource(storage.data(), storage.size());

  const auto message =
      microfmt::pmr::format(&resource, "temperature={} C", 24);

  microfmt::pmr::arena_sink packet(&resource, 16);
  microfmt::format_to(packet.as_sink(), "id={:04x}, status={}", 0x2a,
                      "ready");

  std::printf("%s\npacket: %.*s\n", message.c_str(),
              static_cast<int>(packet.view().size()), packet.view().data());
}
