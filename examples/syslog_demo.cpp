#include <cstdint>
#include <microfmt/formatters/bitfield.hpp>
#include <microfmt/log/logger.hpp>
#include <microfmt/formatters/net.hpp>
#include <microfmt/sinks/syslog_sink.hpp>

int main() {
  ::openlog("microfmt_daemon", LOG_PID | LOG_NDELAY, LOG_DAEMON);

  microfmt::log::syslog_sink<256> syslog;
  microfmt::log::basic_logger<1, 256> logger("microfmt_daemon",
                                               syslog.as_sink());
  logger.set_level(microfmt::log::level::trace);
  logger.info("Daemon started successfully on core {:d}", 0);

  const uint8_t eth_mac[] = {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E};
  logger.warn("Network interface eth0 link down (MAC: {:X})",
              microfmt::mac(eth_mac));

  const uint32_t err_reg = 0x00000004;
  constexpr microfmt::bit_field BUS_ERRS[] = {
      MICROFMT_BIT_FLAG(0, "TIMEOUT"),
      MICROFMT_BIT_FLAG(1, "PARITY"),
      MICROFMT_BIT_FLAG(2, "ADDR_NACK")
  };

  logger.error("Bus Controller Fault: {}",
               microfmt::bits(err_reg, microfmt::span(BUS_ERRS)));
  logger.flush();

  ::closelog();
  return 0;
}
