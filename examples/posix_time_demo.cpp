// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <cstdint>
#include <ctime>
#include <microfmt/formatters/posix_time.hpp>
#include <microfmt/microfmt.hpp>
#include <microfmt/sinks/stdio.hpp>

#if defined(_WIN32)
// Windows/Bare-metal mocks if <sys/time.h> is unavailable
struct timeval {
  long tv_sec;
  long tv_usec;
};
#else
#include <sys/time.h>
#endif

// Helper to compute time difference between two timespec snapshots
constexpr timespec diff_timespec(const timespec &start,
                                 const timespec &end) noexcept {
  timespec result{};
  if ((end.tv_nsec - start.tv_nsec) < 0) {
    result.tv_sec = end.tv_sec - start.tv_sec - 1;
    result.tv_nsec = 1'000'000'000 + end.tv_nsec - start.tv_nsec;
  } else {
    result.tv_sec = end.tv_sec - start.tv_sec;
    result.tv_nsec = end.tv_nsec - start.tv_nsec;
  }
  return result;
}

int main() {
  auto out = microfmt::stdout_sink();

  microfmt::println(out, "=== POSIX Time Formatter Demo ===");

  // struct timespec Formatting
  const timespec epoch_stamp{1715420000, 456123789}; // Nanosecond timestamp
  microfmt::println(out, "\n--- struct timespec ---");
  microfmt::println(out, "Default (ns)  : {}", epoch_stamp);
  microfmt::println(out, "Millis  ({:m}) : {:m}", epoch_stamp, epoch_stamp);
  microfmt::println(out, "Micros  ({:u}) : {:u}", epoch_stamp, epoch_stamp);
  microfmt::println(out, "Raw No Suffix : {:r}", epoch_stamp);

  // struct timeval Formatting
  const timeval tv_stamp{1715420000, 456123}; // Microsecond timestamp
  microfmt::println(out, "\n--- struct timeval ---");
  microfmt::println(out, "Default (us)  : {}", tv_stamp);
  microfmt::println(out, "Millis  ({:m}) : {:m}", tv_stamp, tv_stamp);
  microfmt::println(out, "Raw No Suffix : {:r}", tv_stamp);

  // Execution Latency / Delta Profiling
  const timespec t_start{100, 250'000'000};
  const timespec t_end{100, 254'320'150};
  const timespec latency = diff_timespec(t_start, t_end);

  microfmt::println(out, "\n--- Delta / Latency Profile ---");
  microfmt::println(out, "Start Time    : {}", t_start);
  microfmt::println(out, "End Time      : {}", t_end);
  microfmt::println(out, "Task Latency  : {:u} ({})", latency, latency);

  return 0;
}
