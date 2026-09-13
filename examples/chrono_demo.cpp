#include <chrono>

#include <microfmt/formatters/chrono.hpp>
#include <microfmt/sinks/stdio.hpp>

int main() {
  using namespace std::chrono;

  const system_clock::time_point timestamp{milliseconds{97445006}};
  const steady_clock::time_point uptime{milliseconds{3723004}};

  microfmt::println("Timeout:   {}", milliseconds{250});
  microfmt::println("Timestamp: {}", timestamp);
  microfmt::println("Date:      {:d}", timestamp);
  microfmt::println("Time:      {:t}", timestamp);
  microfmt::println("Uptime:    {}", uptime);

  return 0;
}
