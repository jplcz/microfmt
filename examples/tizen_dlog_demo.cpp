// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/log/logger.hpp>
#include <microfmt/sinks/tizen_dlog_sink.hpp>

int main() {
  microfmt::log::tizen_dlog_sink dlog("microfmt");
  microfmt::log::basic_logger<1, 256> logger("telemetry", dlog.as_sink());

  logger.info("Application started");
  logger.warn("Battery voltage is {} mV", 3200);
  logger.error("Sensor read failed with code {}", 5);
  logger.flush();
}
