// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/log/logger.hpp>

int main() {
  microfmt::log::stdout_color_sink<128> console;
  microfmt::log::logger application_logger("application", console.as_sink());
  application_logger.set_level(microfmt::log::level::trace);

  microfmt::log::set_default_logger(&application_logger);
  microfmt::log::debug("Default logger configured");
  microfmt::log::info("Request {} completed in {} ms", 42, 3);
  microfmt::log::set_default_logger(nullptr);
}
