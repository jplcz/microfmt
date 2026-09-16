// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/log/logger.hpp>
#include <microfmt/sinks/android_log_sink.hpp>

int main() {
  microfmt::log::android_log_sink android("microfmt");
  microfmt::log::basic_logger<1, 256> logger("telemetry", android.as_sink());

  logger.info("Connected to sensor {}", 7);
  logger.warn("Battery voltage is {} mV", 3200);
  logger.error("Sensor read failed with code {}", 5);
  logger.flush();
}
