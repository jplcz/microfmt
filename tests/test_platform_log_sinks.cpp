// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

#include <microfmt/log/log_msg.hpp>
#include <microfmt/sinks/android_log_sink.hpp>
#if defined(MICROFMT_COMPILE_WITH_SYSLOG)
#include <microfmt/sinks/syslog_sink.hpp>
#endif

namespace {

#if defined(MICROFMT_COMPILE_WITH_SYSLOG)
struct syslog_record {
  int priority{0};
  std::array<char, 128> message{};
  std::size_t message_size{0};
};
#endif

struct android_record {
  int priority{0};
  std::array<char, 64> tag{};
  std::size_t tag_size{0};
  std::array<char, 128> message{};
  std::size_t message_size{0};
};

#if defined(MICROFMT_COMPILE_WITH_SYSLOG)
syslog_record captured_syslog;
#endif
android_record captured_android_log;

MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE

void copy_to_buffer(microfmt::string_view text, char *destination, std::size_t capacity, std::size_t &size) noexcept {
  size = text.size() < capacity ? text.size() : capacity;
  for (std::size_t i = 0; i < size; ++i) {
    destination[i] = text[i];
  }
}
MICROFMT_END_UNSAFE_BUFFER_USAGE

#if defined(MICROFMT_COMPILE_WITH_SYSLOG)
void capture_syslog(int priority, microfmt::string_view message) noexcept {
  captured_syslog.priority = priority;
  copy_to_buffer(message, captured_syslog.message.data(), captured_syslog.message.size(), captured_syslog.message_size);
}
#endif

void capture_android_log(int priority, microfmt::string_view tag, microfmt::string_view message) noexcept {
  captured_android_log.priority = priority;
  copy_to_buffer(tag, captured_android_log.tag.data(), captured_android_log.tag.size(), captured_android_log.tag_size);
  copy_to_buffer(message, captured_android_log.message.data(), captured_android_log.message.size(),
                 captured_android_log.message_size);
}

} // namespace

#if defined(MICROFMT_COMPILE_WITH_SYSLOG)
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
  EXPECT_EQ(microfmt::string_view(captured_syslog.message.data(), captured_syslog.message_size),
            "[daemon] connection lost");
}
#endif

TEST(PlatformLogSinkTest, AndroidMapsLevelsAndHonorsSinkThreshold) {
  captured_android_log = {};
  microfmt::log::android_log_sink<64, 16> android("microfmt", capture_android_log);
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
  // The originating logger's name is forwarded as the logcat tag.
  EXPECT_EQ(microfmt::string_view(captured_android_log.tag.data(), captured_android_log.tag_size), "sensor");
  EXPECT_EQ(microfmt::string_view(captured_android_log.message.data(), captured_android_log.message_size),
            "overheat");
}

TEST(PlatformLogSinkTest, AndroidFallsBackToSinkTagWhenLoggerNameIsEmpty) {
  captured_android_log = {};
  microfmt::log::android_log_sink<64, 16> android("microfmt", capture_android_log);
  auto output = android.as_sink();

  output.log(microfmt::log::log_msg{
      .lvl = microfmt::log::level::info,
      .payload = "no logger name",
  });

  EXPECT_EQ(microfmt::string_view(captured_android_log.tag.data(), captured_android_log.tag_size), "microfmt");
}
