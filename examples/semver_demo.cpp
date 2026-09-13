#include <microfmt/formatters/semver.hpp>
#include <microfmt/sinks/stdio.hpp>

int main() {
  const auto release = microfmt::version(1, 4, 0, "rc.2", "git.a1b2c3d");

  microfmt::println("Release: {}", release);
  microfmt::println("Core:    {:c}", release);
  microfmt::println("Tagged:  {:#}", release);
  microfmt::println("Packed:  {}",
                    microfmt::from_packed32(0x02030004u));

  return 0;
}
