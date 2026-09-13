#include <microfmt/log/logger.hpp>
#include <microfmt/sinks/systemd_sink.hpp>

int main() {
  microfmt::log::systemd_sink journal;
  microfmt::log::basic_logger<1, 256> logger("microfmt-service",
                                               journal.as_sink());

  logger.info("Service started");
  logger.warn("Worker {} is running behind schedule", 3);
  logger.error("Unable to process request {}", 42);
  logger.flush();
}
