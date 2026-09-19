// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/inspector/task.hpp>
#include <microfmt/sinks/stdio.hpp>

namespace {

struct thread_sequence {
  const microfmt::thread_info *threads;
  std::size_t count;
  std::size_t index;
};

bool next_thread(void *ctx, microfmt::thread_info &out) noexcept {
  auto &sequence = *static_cast<thread_sequence *>(ctx);
  if (sequence.index >= sequence.count) {
    return false;
  }
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  out = sequence.threads[sequence.index++];

  RELOCO_END_UNSAFE_BUFFER_USAGE;

  return true;
}

} // namespace

int main() {
  const microfmt::thread_info threads[]{
      {.thread_id = 101,
       .name = "telemetry-main",
       .priority = 8,
       .state = microfmt::thread_state::running,
       .cpu_ticks = 18420,
       .stack_size = 4096,
       .stack_usage = 1760},
      {.thread_id = 102,
       .name = "network-rx",
       .priority = 10,
       .state = microfmt::thread_state::waiting,
       .cpu_ticks = 7350,
       .stack_size = 3072,
       .stack_usage = 1280},
  };
  thread_sequence sequence{threads, 2, 0};
  const microfmt::task_info task{
      .team_id = 42,
      .name = "telemetry",
      .state = microfmt::task_state::running,
      .priority = 8,
      .cpu_ticks = 25770,
      .memory_usage = 65536,
      .thread_next_fn = next_thread,
      .thread_next_ctx = &sequence,
  };

  auto output = microfmt::stdout_sink();
  microfmt::md::writer document(output);
  char value_scratch[64];

  document.h1("Task and Thread Report").newline();

  microfmt::task_info::metadata_state task_metadata;
  microfmt::md::write_metadata_table(document, task.make_metadata_view(task_metadata), value_scratch, "Task Metadata");

  microfmt::thread_info thread;
  while (task.get_next_thread(thread)) {
    document.newline();
    microfmt::thread_info::metadata_state thread_metadata;
    microfmt::md::write_metadata_table(document, thread.make_metadata_view(thread_metadata), value_scratch,
                                       "Thread Metadata");
  }

  return 0;
}
