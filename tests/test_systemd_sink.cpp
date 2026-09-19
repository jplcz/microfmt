// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

#include <microfmt/log/log_msg.hpp>
#include <microfmt/sinks/systemd_sink.hpp>

namespace {

struct journal_record {
  int priority{0};
  std::array<char, 64> identifier{};
  std::size_t identifier_size{0};
  std::array<char, 128> message{};
  std::size_t message_size{0};
};

journal_record captured_journal;

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

void copy_to_buffer(microfmt::string_view text, char *destination, std::size_t capacity, std::size_t &size) noexcept {
  size = text.size() < capacity ? text.size() : capacity;
  for (std::size_t i = 0; i < size; ++i) {
    destination[i] = text[i];
  }
}

RELOCO_END_UNSAFE_BUFFER_USAGE

void capture_journal(int priority, microfmt::string_view identifier, microfmt::string_view message) noexcept {
  captured_journal.priority = priority;
  copy_to_buffer(identifier, captured_journal.identifier.data(), captured_journal.identifier.size(),
                 captured_journal.identifier_size);
  copy_to_buffer(message, captured_journal.message.data(), captured_journal.message.size(),
                 captured_journal.message_size);
}

} // namespace

TEST(SystemdSinkTest, MapsSeverityAndForwardsStructuredFields) {
  captured_journal = {};
  microfmt::log::systemd_sink<128, 32> journal(capture_journal);
  auto output = journal.as_sink();

  output.log(microfmt::log::log_msg{
      .logger_name = "telemetry",
      .lvl = microfmt::log::level::err,
      .payload = "sensor timeout",
  });

  EXPECT_EQ(captured_journal.priority, LOG_ERR);
  EXPECT_EQ(microfmt::string_view(captured_journal.identifier.data(), captured_journal.identifier_size), "telemetry");
  EXPECT_EQ(microfmt::string_view(captured_journal.message.data(), captured_journal.message_size), "sensor timeout");
}

TEST(SystemdSinkTest, HonorsConfiguredSinkThreshold) {
  captured_journal = {};
  microfmt::log::systemd_sink<> journal(capture_journal);
  auto output = journal.as_sink();
  output.set_level(microfmt::log::level::critical);

  output.log(microfmt::log::log_msg{
      .logger_name = "telemetry",
      .lvl = microfmt::log::level::warn,
      .payload = "low battery",
  });

  EXPECT_EQ(captured_journal.message_size, 0U);
}
