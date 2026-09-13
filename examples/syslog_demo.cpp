#include <cstdint>
#include <microfmt/microfmt.hpp>
#include <microfmt/sinks/syslog_sink.hpp>
#include <microfmt/bitfield.hpp>
#include <microfmt/net.hpp>

int main() {
  ::openlog("microfmt_daemon", LOG_PID | LOG_NDELAY, LOG_DAEMON);

  // One-liner direct syslog logging
  microfmt::syslog(microfmt::log_priority::info, 
                   "Daemon started successfully on core {:d}", 0);

  // Line-buffered streaming sink (flushes each '\n')
  microfmt::syslog_sink<256> sys_sink(microfmt::log_priority::warning);
  auto out = sys_sink.as_sink();

  const uint8_t eth_mac[] = {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E};
  microfmt::format_to(out, "Network interface eth0 link down (MAC: {:X})\n", 
                      microfmt::mac(eth_mac));

  // Change priority on the fly
  sys_sink.set_priority(microfmt::log_priority::err);

  const uint32_t err_reg = 0x00000004;
  constexpr microfmt::bit_field BUS_ERRS[] = {
      MICROFMT_BIT_FLAG(0, "TIMEOUT"),
      MICROFMT_BIT_FLAG(1, "PARITY"),
      MICROFMT_BIT_FLAG(2, "ADDR_NACK")
  };

  // Multiple writes buffered into a single syslog message
  microfmt::format_to(out, "Bus Controller Fault: ");
  microfmt::format_to(out, "{}\n", microfmt::bits(err_reg, microfmt::span(BUS_ERRS)));

  ::closelog();
  return 0;
}
