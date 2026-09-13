#include <cstdint>

#include <microfmt/formatters/i2c.hpp>
#include <microfmt/sinks/stdio.hpp>

int main() {
  const uint8_t who_am_i_register[] = {0x75};
  const uint8_t who_am_i_value[] = {0x68};
  const uint8_t calibration_data[] = {0x00, 0xF4, 0x01, 0x90};
  const uint8_t ten_bit_response[] = {0xDE, 0xAD};

  const auto select_register =
      microfmt::i2c_write(0x68, who_am_i_register);
  const auto read_identity = microfmt::i2c_read(0x68, who_am_i_value);
  const auto read_calibration =
      microfmt::i2c_read(0x68, calibration_data, microfmt::i2c_status::ok);
  const auto failed_ten_bit_read = microfmt::i2c_10bit(
      0x2AB, microfmt::i2c_dir::read, microfmt::span(ten_bit_response),
      microfmt::i2c_status::timeout);

  microfmt::println("=== Synthesized I2C traffic ===");
  microfmt::println("{}", select_register);
  microfmt::println("{}", read_identity);
  microfmt::println("{}", read_calibration);
  microfmt::println("{}", failed_ten_bit_read);
  microfmt::println("\n=== Compact trace ===");
  microfmt::println("{:c}", select_register);
  microfmt::println("{:c}", read_identity);
  microfmt::println("{:c}", failed_ten_bit_read);
}
