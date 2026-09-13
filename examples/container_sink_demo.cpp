#include <cstdio>
#include <string>
#include <vector>

#include <microfmt/sinks/container_sink.hpp>

int main() {
  std::string message = "telemetry: ";
  microfmt::format_to_container(message, "temperature={} C", 24);

  const auto packet = microfmt::format_as_container<std::vector<char>>(
      "id={:04x}, status={}", 0x2a, "ready");

  std::printf("%s\npacket: %.*s\n", message.c_str(),
              static_cast<int>(packet.size()), packet.data());
}
