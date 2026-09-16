// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>

#include <microfmt/formatters/spi.hpp>
#include <microfmt/sinks/stdio.hpp>

int main() {
  const uint8_t flash_read_command[] = {0x0B, 0x00, 0x10, 0x00, 0x00};
  const uint8_t flash_read_response[] = {0x00, 0x00, 0x00, 0x00, 0xEF};
  const uint8_t imu_config[] = {0x20, 0x47};
  const uint8_t sensor_response[] = {0x00, 0x00, 0x00, 0x00};

  const auto flash_read = microfmt::spi_duplex(
      flash_read_command, flash_read_response, 0, microfmt::spi_mode::mode0);
  const auto configure_imu =
      microfmt::spi_write(imu_config, 1, microfmt::spi_mode::mode3);
  const auto failed_sensor_read =
      microfmt::spi_read(sensor_response, 1, microfmt::spi_mode::mode3,
                         microfmt::spi_status::timeout);

  microfmt::println("=== Synthesized SPI traffic ===");
  microfmt::println("{}", flash_read);
  microfmt::println("{}", configure_imu);
  microfmt::println("{}", failed_sensor_read);
  microfmt::println("\n=== Compact trace ===");
  microfmt::println("{:c}", flash_read);
  microfmt::println("{:c}", configure_imu);
  microfmt::println("{:c}", failed_sensor_read);
}
