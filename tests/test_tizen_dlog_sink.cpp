// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string_view>

#include <microfmt/log/log_msg.hpp>
#include <microfmt/sinks/tizen_dlog_sink.hpp>

namespace {

struct dlog_record {
  int priority{0};
  std::array<char, 64> tag{};
  std::size_t tag_size{0};
  std::array<char, 128> message{};
  std::size_t message_size{0};
};

dlog_record captured_dlog;

void copy_to_buffer(std::string_view text, char *destination,
                    std::size_t capacity, std::size_t &size) noexcept {
  size = text.size() < capacity ? text.size() : capacity;
  for (std::size_t i = 0; i < size; ++i) {
    destination[i] = text[i];
  }
}

void capture_dlog(int priority, microfmt::string_view tag,
                  microfmt::string_view message) noexcept {
  captured_dlog.priority = priority;
  copy_to_buffer(tag, captured_dlog.tag.data(), captured_dlog.tag.size(),
                 captured_dlog.tag_size);
  copy_to_buffer(message, captured_dlog.message.data(),
                 captured_dlog.message.size(), captured_dlog.message_size);
}

} // namespace

TEST(TizenDlogSinkTest, MapsSeverityAndForwardsLogRecord) {
  captured_dlog = {};
  microfmt::log::tizen_dlog_sink<32> dlog("microfmt", capture_dlog);
  auto output = dlog.as_sink();

  output.log(microfmt::log::log_msg{
      .logger_name = "telemetry",
      .lvl = microfmt::log::level::critical,
      .payload = "sensor failure",
  });

  EXPECT_EQ(captured_dlog.priority, DLOG_FATAL);
  EXPECT_EQ(std::string_view(captured_dlog.tag.data(), captured_dlog.tag_size),
            "microfmt");
  EXPECT_EQ(
      std::string_view(captured_dlog.message.data(), captured_dlog.message_size),
      "sensor failure");
}

TEST(TizenDlogSinkTest, HonorsConfiguredSinkThreshold) {
  captured_dlog = {};
  microfmt::log::tizen_dlog_sink<> dlog("microfmt", capture_dlog);
  auto output = dlog.as_sink();
  output.set_level(microfmt::log::level::err);

  output.log(microfmt::log::log_msg{
      .logger_name = "telemetry",
      .lvl = microfmt::log::level::warn,
      .payload = "low battery",
  });

  EXPECT_EQ(captured_dlog.message_size, 0U);
}
