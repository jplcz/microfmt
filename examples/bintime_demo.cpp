#include <microfmt/formatters/bintime.hpp>
#include <microfmt/microfmt.hpp>
#include <microfmt/sinks/stdio.hpp>

// Native FreeBSD Kernel/Userland header
#if defined(__FreeBSD__)
#include <sys/time.h>
#else
// Mock for non-FreeBSD platforms to demonstrate identical ABI/struct
struct bintime {
  time_t sec;
  uint64_t frac;
};
using sbintime_t = int64_t;
#endif

int main() {
  auto out = microfmt::stdout_sink();

  // Native struct bintime formats automatically via structural detection
  struct bintime bt;
  bt.sec = 1715420000;
  bt.frac = 0x8000000000000000ULL; // 0.5 sec

  microfmt::println(out, "Native bintime        : {}", bt);
  microfmt::println(out, "Native bintime (ms)   : {:m}", bt);
  microfmt::println(out, "Native bintime (pico) : {:p}", bt);

  // Native sbintime_t formatted via as_sbintime adapter
  sbintime_t sbt_raw = (static_cast<int64_t>(5) << 32) | (1ULL << 31); // 5.5s
  microfmt::println(out, "Native sbintime       : {}",
                    microfmt::as_sbintime(sbt_raw));

  return 0;
}
