// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <cstring>
#include <microfmt/inspector/fallible_address_space.hpp>
#include <microfmt/sinks/stdio.hpp>

// ============================================================================
// Data Structure
// ============================================================================

struct TaskInfo {
  uint64_t id;
  uint32_t priority;
  char name[16];
};

template <> struct microfmt::formatter<TaskInfo> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const TaskInfo &task, const sink &out) const noexcept {
    microfmt::format_to(out, "TaskInfo(id={}, priority={}, name={})", task.id, task.priority, task.name);
  }
};

// ============================================================================
// Main Demonstration
// ============================================================================

int main() {
  microfmt::println("=========================================================="
                    "======================");
  microfmt::println("      microfmt Fallible Local Address Space Demo");
  microfmt::println("=========================================================="
                    "======================");

  const microfmt::address_space_ref space(microfmt::fallible_local_space_tag{});

  // --------------------------------------------------------------------------
  // Scenario A: Successful read/write against a valid local object
  // --------------------------------------------------------------------------
  TaskInfo task{1024, 5, "worker"};

  alignas(alignof(TaskInfo)) std::byte task_scratch[sizeof(TaskInfo)];
  microfmt::remote_ref<TaskInfo> task_ref(reinterpret_cast<uintptr_t>(&task), space, task_scratch);

  microfmt::println("\n[1. Valid Address]");
  microfmt::println("Task via remote_ref : {}", task_ref);

  auto loaded = task_ref.load();
  if (loaded) {
    microfmt::println("Direct read()       : id={}, priority={}", (*loaded)->id, (*loaded)->priority);
  }

  // A raw write back to the very same object also succeeds.
  uint32_t new_priority = 9;
  auto write_result =
      space.write(reinterpret_cast<uintptr_t>(&task.priority), new_priority);
  microfmt::println("Write succeeded      : {} (task.priority is now {})", write_result.has_value(), task.priority);

  // --------------------------------------------------------------------------
  // Scenario B: Safe reads/writes against deliberately invalid addresses
  // --------------------------------------------------------------------------
  microfmt::println("\n[2. Invalid / Unmapped Addresses (no crash!)]");

  // A wild pointer far outside any mapped region - would normally SIGSEGV.
  constexpr uintptr_t wild_addr = 0x0000'dead'beef'0000ULL;

  int probe_dest = 0;
  auto bad_read = space.read_bytes(wild_addr, &probe_dest, sizeof(probe_dest));
  microfmt::println("read_bytes(wild)     : ok={}", bad_read.has_value());

  int probe_src = 42;
  auto bad_write = space.write_bytes(wild_addr, &probe_src, sizeof(probe_src));
  microfmt::println("write_bytes(wild)    : ok={}", bad_write.has_value());

  // remote_ref / remote_string_view also fail gracefully and format as "<invalid>".
  microfmt::remote_ref<TaskInfo> bad_task_ref(wild_addr, space, task_scratch);
  char str_scratch[32];
  microfmt::remote_string_view bad_str_ref(wild_addr, space, str_scratch);
  microfmt::println("Fault Struct         : {}", bad_task_ref);
  microfmt::println("Fault String         : {}", bad_str_ref);

  // --------------------------------------------------------------------------
  // Scenario C: Fault recovery does not corrupt subsequent, valid operations
  // --------------------------------------------------------------------------
  microfmt::println("\n[3. Recovery Sanity Check]");

  auto still_works = space.read<uint32_t>(reinterpret_cast<uintptr_t>(&task.priority));
  microfmt::println("Read after fault     : ok={}, value={}", still_works.has_value(),
                    still_works.value_or(0));

  // --------------------------------------------------------------------------
  // Scenario D: Safe strings, valid and truncated/unterminated
  // --------------------------------------------------------------------------
  microfmt::println("\n[4. String Reads]");

  microfmt::remote_string_view good_str_ref(reinterpret_cast<uintptr_t>(task.name), space, str_scratch);
  microfmt::println("Valid Name String    : {}", good_str_ref);

  return 0;
}
