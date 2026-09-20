// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>

#include <reloco/default_allocator.hpp>
#include <reloco/flat_set.hpp>
#include <reloco/vector.hpp>

#include <microfmt/formatters/json.hpp>
#include <microfmt/formatters/reloco.hpp>
#include <microfmt/sinks/stdio.hpp>

int main() {
  // Gather Telemetry using fallible zero-allocation containers
  auto faults_res = reloco::flat_set<uint16_t>::try_create();
  if (!faults_res)
    return -1;
  auto &faults = *faults_res;

  // Emulate hardware errors (flat_set ignores duplicates and keeps them sorted)
  std::ignore = faults.try_insert(0x0A01);
  std::ignore = faults.try_insert(0x0C20);
  std::ignore = faults.try_insert(0x0A01); // Ignored

  auto temps_res = reloco::vector<int8_t>::try_create();
  if (!temps_res)
    return -1;
  auto &temps = *temps_res;

  std::ignore = temps.try_push_back(42);
  std::ignore = temps.try_push_back(44);
  std::ignore = temps.try_push_back(45);

  // Standalone Structured Logging
  // Output: [0A01, 0C20] -> Format specifier {:04X} cascades to the elements natively!
  microfmt::println("system.faults = {:04X}", microfmt::as_collection_view(faults));
  microfmt::println("system.temps  = {}", microfmt::as_collection_view(temps));

  // 3. Nested JSON Payload Generation
  microfmt::buffer_sink<512> output;
  {
    microfmt::json::object_writer telemetry(output.as_sink());
    telemetry.kv("device", "controller-arm64").kv("uptime_s", 86400);

    {
      auto diag = telemetry.nested_object("diagnostics");
      diag.kv("is_healthy", faults.empty());

      // Iterate the reloco::vector to populate the JSON array cleanly
      auto temp_arr = diag.nested_array("temperatures_c");
      for (const auto t : temps) {
        temp_arr.val(t);
      }
      temp_arr.end();

      // Iterate the reloco::flat_set for the JSON array
      auto fault_arr = diag.nested_array("active_fault_codes");
      for (const auto f : faults) {
        fault_arr.val(f);
      }
      fault_arr.end();
    }
  }

  // Flush the full JSON payload to stdout
  microfmt::println("\n[TX] {}", output.view());

  reloco::vector<int> my_vec;
  std::ignore = my_vec.try_push_back(10);
  std::ignore = my_vec.try_push_back(20);

  // Bind the mutable ref dynamically
  reloco::mutable_container_ref<int> seq_ref(my_vec);

  // Formats as: [0A, 14] (Formatting cascades natively to the underlying elements)
  microfmt::println("vector contents: {:02X}", seq_ref);

  return 0;
}