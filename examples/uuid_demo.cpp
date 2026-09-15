// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <cstdio>
#include <microfmt/microfmt.hpp>
#include <microfmt/sinks/stdio.hpp>
#include <microfmt/formatters/uuid.hpp>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>

int main() {
  boost::uuids::nil_generator nil_gen;
  boost::uuids::string_generator string_gen;

  boost::uuids::uuid nil_id = nil_gen();
  boost::uuids::uuid node_id =
      string_gen("6ba7b810-9dad-11d1-80b4-00c04fd430c8");

  microfmt::println("=== Boost UUID Formatting Demo ===");
  microfmt::println("Nil UUID          : {}", nil_id);
  microfmt::println("Canonical (lower) : {}", node_id);

  // Escape literal braces using {{ and }}
  microfmt::println("Uppercase {{:X}}    : {:X}", node_id);
  microfmt::println("Braced {{:#}}       : {:#}", node_id);
  microfmt::println("Combined {{:#X}}    : {:#X}", node_id);

  // Helper view
  auto custom_view =
      microfmt::uuid(node_id, /*uppercase=*/true, /*braced=*/true);
  microfmt::println("Via helper view   : {}", custom_view);

  // Stack buffer
  auto buf = microfmt::format<64>("Device GUID: {}", node_id);
  std::fwrite(buf.view().data(), 1, buf.size(), stdout);
  std::putchar('\n');

  return 0;
}
