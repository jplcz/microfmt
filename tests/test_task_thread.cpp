// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/inspector/task.hpp>
#include <microfmt/inspector/thread.hpp>

#include <type_traits>
#include <utility>

namespace {

struct extra_metadata_state {
  int value;
  bool emitted;
};

bool next_extra_metadata(void *ctx, microfmt::property_entry &out) noexcept {
  auto &state = *static_cast<extra_metadata_state *>(ctx);
  if (state.emitted) {
    return false;
  }
  state.emitted = true;
  out.key = "core";
  out.val_ptr = &state.value;
  out.print_fn = [](const void *ptr, const microfmt::sink &sink) noexcept {
    microfmt::formatter<int> fmt;
    fmt.format(*static_cast<const int *>(ptr), sink);
  };
  return true;
}

struct thread_sequence {
  const microfmt::thread_info *threads;
  std::size_t size;
  std::size_t index;
};

bool next_thread(void *ctx, microfmt::thread_info &out) noexcept {
  auto &sequence = *static_cast<thread_sequence *>(ctx);
  if (sequence.index == sequence.size) {
    return false;
  }

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  out = sequence.threads[sequence.index++];
  RELOCO_END_UNSAFE_BUFFER_USAGE;

  return true;
}

template <typename T, typename = void> struct can_make_metadata_from_rvalue : std::false_type {};

template <typename T>
struct can_make_metadata_from_rvalue<T, std::void_t<decltype(std::declval<const T &&>().make_metadata_view(
                                            std::declval<typename T::metadata_state &>()))>> : std::true_type {};

static_assert(!can_make_metadata_from_rvalue<microfmt::task_info>::value);
static_assert(!can_make_metadata_from_rvalue<microfmt::thread_info>::value);

TEST(TaskState, FormatsEveryState) {
  EXPECT_EQ(microfmt::format<16>("{}", microfmt::task_state::ready).view(), "READY");
  EXPECT_EQ(microfmt::format<16>("{}", microfmt::task_state::running).view(), "RUNNING");
  EXPECT_EQ(microfmt::format<16>("{}", microfmt::task_state::suspended).view(), "SUSPENDED");
  EXPECT_EQ(microfmt::format<16>("{}", microfmt::task_state::waiting).view(), "WAITING");
  EXPECT_EQ(microfmt::format<16>("{}", microfmt::task_state::terminated).view(), "TERMINATED");
}

TEST(ThreadState, FormatsEveryState) {
  EXPECT_EQ(microfmt::format<16>("{}", microfmt::thread_state::ready).view(), "READY");
  EXPECT_EQ(microfmt::format<16>("{}", microfmt::thread_state::running).view(), "RUNNING");
  EXPECT_EQ(microfmt::format<16>("{}", microfmt::thread_state::suspended).view(), "SUSPENDED");
  EXPECT_EQ(microfmt::format<16>("{}", microfmt::thread_state::waiting).view(), "WAITING");
  EXPECT_EQ(microfmt::format<16>("{}", microfmt::thread_state::sleeping).view(), "SLEEPING");
  EXPECT_EQ(microfmt::format<16>("{}", microfmt::thread_state::terminated).view(), "TERMINATED");
}

TEST(ThreadInfo, ExposesCoreAndExtendedMetadata) {
  extra_metadata_state extra{3, false};
  microfmt::thread_info thread;
  thread.thread_id = 17;
  thread.name = "worker";
  thread.priority = 8;
  thread.state = microfmt::thread_state::running;
  thread.cpu_ticks = 120;
  thread.stack_size = 4096;
  thread.stack_usage = 1536;
  thread.extended_metadata_fn = next_extra_metadata;
  thread.extended_metadata_ctx = &extra;
  microfmt::thread_info::metadata_state state;

  EXPECT_EQ(microfmt::format<256>("{}", thread.make_metadata_view(state)).view(),
            "{\"thread_id\": 17, \"name\": \"worker\", \"priority\": 8, "
            "\"state\": RUNNING, \"cpu_ticks\": 120, \"stack_size_bytes\": "
            "4096, \"stack_usage_bytes\": 1536, \"core\": 3}");
}

TEST(TaskInfo, ExposesCoreAndExtendedMetadata) {
  extra_metadata_state extra{2, false};
  microfmt::task_info task;
  task.team_id = 41;
  task.name = "telemetry";
  task.state = microfmt::task_state::waiting;
  task.priority = 5;
  task.cpu_ticks = 900;
  task.memory_usage = 32768;
  task.extended_metadata_fn = next_extra_metadata;
  task.extended_metadata_ctx = &extra;
  microfmt::task_info::metadata_state state;

  EXPECT_EQ(microfmt::format<256>("{}", task.make_metadata_view(state)).view(),
            "{\"team_id\": 41, \"name\": \"telemetry\", \"state\": WAITING, "
            "\"priority\": 5, \"cpu_ticks\": 900, \"memory_usage\": 32768, "
            "\"core\": 2}");
}

TEST(TaskInfo, IteratesThreadsAndHandlesMissingGenerator) {
  microfmt::thread_info threads[2];
  threads[0].thread_id = 1;
  threads[0].name = "main";
  threads[1].thread_id = 2;
  threads[1].name = "worker";
  thread_sequence sequence{threads, 2, 0};
  microfmt::task_info task;
  task.thread_next_fn = next_thread;
  task.thread_next_ctx = &sequence;
  microfmt::thread_info thread;

  ASSERT_TRUE(task.get_next_thread(thread));
  EXPECT_EQ(thread.thread_id, 1U);
  ASSERT_TRUE(task.get_next_thread(thread));
  EXPECT_EQ(thread.thread_id, 2U);
  EXPECT_FALSE(task.get_next_thread(thread));

  const microfmt::task_info empty;
  EXPECT_FALSE(empty.get_next_thread(thread));
}

TEST(TaskAndThreadInfo, MetadataViewCanBeReset) {
  microfmt::thread_info thread;
  thread.thread_id = 8;
  thread.name = "idle";
  microfmt::thread_info::metadata_state state;

  const auto first = microfmt::format<256>("{}", thread.make_metadata_view(state));
  const auto second = microfmt::format<256>("{}", thread.make_metadata_view(state));

  EXPECT_EQ(first.view(), second.view());
}

} // namespace
