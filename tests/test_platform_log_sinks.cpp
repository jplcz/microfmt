// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string_view>

#include <microfmt/log/log_msg.hpp>
#include <microfmt/sinks/android_log_sink.hpp>
#include <microfmt/sinks/syslog_sink.hpp>

namespace {

struct syslog_record {
  int priority{0};
  std::array<char, 128> message{};
  std::size_t message_size{0};
};

struct android_record {
  int priority{0};
  std::array<char, 64> tag{};
  std::size_t tag_size{0};
  std::array<char, 128> message{};
  std::size_t message_size{0};
};

syslog_record captured_syslog;
android_record captured_android_log;

void copy_to_buffer(std::string_view text, char *destination,
                    std::size_t capacity, std::size_t &size) noexcept {
  size = text.size() < capacity ? text.size() : capacity;
  for (std::size_t i = 0; i < size; ++i) {
    destination[i] = text[i];
  }
}

void capture_syslog(int priority, std::string_view message) noexcept {
  captured_syslog.priority = priority;
  copy_to_buffer(message, captured_syslog.message.data(),
                 captured_syslog.message.size(), captured_syslog.message_size);
}

void capture_android_log(int priority, std::string_view tag,
                         std::string_view message) noexcept {
  captured_android_log.priority = priority;
  copy_to_buffer(tag, captured_android_log.tag.data(),
                 captured_android_log.tag.size(), captured_android_log.tag_size);
  copy_to_buffer(message, captured_android_log.message.data(),
                 captured_android_log.message.size(),
                 captured_android_log.message_size);
}

} // namespace

TEST(PlatformLogSinkTest, SyslogMapsLevelAndIncludesLoggerName) {
  captured_syslog = {};
  microfmt::log::syslog_sink<64> syslog(capture_syslog);
  auto output = syslog.as_sink();
  const microfmt::log::log_msg message{
      .logger_name = "daemon",
      .lvl = microfmt::log::level::warn,
      .payload = "connection lost",
  };

  output.log(message);

  EXPECT_EQ(captured_syslog.priority, LOG_WARNING);
  EXPECT_EQ(std::string_view(captured_syslog.message.data(),
                             captured_syslog.message_size),
            "[daemon] connection lost");
}

TEST(PlatformLogSinkTest, AndroidMapsLevelsAndHonorsSinkThreshold) {
  captured_android_log = {};
  microfmt::log::android_log_sink<64, 16> android("microfmt",
                                                    capture_android_log);
  auto output = android.as_sink();
  output.set_level(microfmt::log::level::err);

  output.log(microfmt::log::log_msg{
      .logger_name = "sensor",
      .lvl = microfmt::log::level::info,
      .payload = "ready",
  });
  EXPECT_EQ(captured_android_log.message_size, 0U);

  output.log(microfmt::log::log_msg{
      .logger_name = "sensor",
      .lvl = microfmt::log::level::err,
      .payload = "overheat",
  });

  EXPECT_EQ(captured_android_log.priority, ANDROID_LOG_ERROR);
  EXPECT_EQ(std::string_view(captured_android_log.tag.data(),
                             captured_android_log.tag_size),
            "microfmt");
  EXPECT_EQ(std::string_view(captured_android_log.message.data(),
                             captured_android_log.message_size),
            "[sensor] overheat");
}
