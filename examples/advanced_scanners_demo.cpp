// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/inspector/advanced_scanners.hpp>
#include <microfmt/sinks/stdio.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

struct task_record {
  uint32_t magic;
  uint16_t state;
  uint16_t priority;
};

struct task_requirements {
  uint32_t magic;
  uint16_t highest_state;
  uint16_t highest_priority;
};

bool is_plausible_task(const void *bytes, void *opaque) noexcept {
  if (!bytes || !opaque)
    return false;

  task_record candidate{};
  MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;
  std::memcpy(&candidate, bytes, sizeof(candidate));
  MICROFMT_END_UNSAFE_BUFFER_USAGE;

  const auto &requirements = *static_cast<const task_requirements *>(opaque);
  return candidate.magic == requirements.magic &&
         candidate.state <= requirements.highest_state &&
         candidate.priority <= requirements.highest_priority;
}

} // namespace

int main() {
  const auto space =
      microfmt::address_space_ref{microfmt::local_space_tag{}};

  const std::array<uint32_t, 6> counters{3, 7, 18, 42, 105, 9};
  std::array<std::byte, 12> range_scratch{};
  microfmt::value_range_scanner<
      microfmt::linear_value_range_scanner_tag<uint32_t>, uint32_t>
      range_scanner{microfmt::linear_memory_scanner_context{
          space, microfmt::span<std::byte>(range_scratch.data(),
                                           range_scratch.size())}};

  const auto range_result = range_scanner.ref().scan(
      reinterpret_cast<uintptr_t>(counters.data()),
      counters.size() * sizeof(counters[0]), 40U, 50U);
  if (range_result && range_result->found) {
    microfmt::println(
        "counter in range at index {}",
        (range_result->address -
         reinterpret_cast<uintptr_t>(counters.data())) /
            sizeof(counters[0]));
  }

  const std::array<task_record, 3> tasks{{
      {0x11111111U, 1, 4},
      {0x5441534bU, 3, 12},
      {0x22222222U, 0, 1},
  }};
  std::array<std::byte, sizeof(task_record) * 2> task_scratch{};
  microfmt::dependent_value_scanner<
      microfmt::linear_dependent_value_scanner_tag>
      task_scanner{microfmt::linear_memory_scanner_context{
          space, microfmt::span<std::byte>(task_scratch.data(),
                                           task_scratch.size())}};
  task_requirements requirements{0x5441534bU, 5, 31};

  const auto task_result = task_scanner.ref().scan(
      reinterpret_cast<uintptr_t>(tasks.data()),
      tasks.size() * sizeof(tasks[0]), sizeof(task_record),
      sizeof(task_record), &is_plausible_task, &requirements);
  if (task_result && task_result->found) {
    microfmt::println(
        "plausible task at index {}",
        (task_result->address - reinterpret_cast<uintptr_t>(tasks.data())) /
            sizeof(tasks[0]));
  }
}
