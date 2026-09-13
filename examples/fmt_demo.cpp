#include <microfmt/fmt.hpp>

int main() {
  // Direct stdout printing
  fmt::println("Booting firmware version {}.{}.{}", 1, 4, 0);

  // format_to_n safety
  char log_buf[32];
  auto res =
      fmt::format_to_n(log_buf, sizeof(log_buf), "ADC: raw={}, ch={}", 1023, 2);
  (void)res;

  // String view formatting
  const char *tags[] = {"boot", "init", "i2c"};
  fmt::println("Stages: [{}]", fmt::join(tags, " -> "));
}
