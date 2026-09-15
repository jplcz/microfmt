// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

#include <microfmt/log/logger.hpp>

namespace {

struct captured_log {
  microfmt::log::level level{microfmt::log::level::off};
  std::array<char, 64> message{};
  std::size_t message_size{0};
  std::size_t count{0};
};

captured_log captured;

void capture_log(void *, const microfmt::log::log_msg &message) noexcept {
  captured.level = message.lvl;
  captured.message_size =
      message.payload.size() < captured.message.size() ? message.payload.size()
                                                       : captured.message.size();
  for (std::size_t i = 0; i < captured.message_size; ++i) {
    captured.message[i] = message.payload[i];
  }
  ++captured.count;
}

microfmt::log::logger application_logger(
    "application", microfmt::log::log_sink{nullptr, capture_log, nullptr,
                                             microfmt::log::level::trace});

} // namespace

#define MICROFMT_DEFAULT_LOGGER application_logger
#include <microfmt/log/macros.hpp>

TEST(LoggerConfigurationTest, DefaultMacrosUseApplicationLoggerOverride) {
  captured = {};
  application_logger.set_level(microfmt::log::level::trace);

  MICROFMT_LOG_INFO("value={}", 42);

  EXPECT_EQ(captured.count, 1U);
  EXPECT_EQ(captured.level, microfmt::log::level::info);
  EXPECT_EQ(
      std::string_view(captured.message.data(), captured.message_size),
      "value=42");
}

TEST(LoggerConfigurationTest, CompileTimeFormatStringsUseLoggerOverloads) {
  captured = {};
  application_logger.set_level(microfmt::log::level::trace);

  application_logger.info(MICROFMT_STRING("value={:04x}"), 0x2a);

  EXPECT_EQ(captured.count, 1U);
  EXPECT_EQ(captured.level, microfmt::log::level::info);
  EXPECT_EQ(
      std::string_view(captured.message.data(), captured.message_size),
      "value=002a");

  captured = {};
  microfmt::log::set_default_logger(&application_logger);
  microfmt::log::warn(MICROFMT_STRING("enabled={}"), true);

  EXPECT_EQ(captured.count, 1U);
  EXPECT_EQ(captured.level, microfmt::log::level::warn);
  EXPECT_EQ(
      std::string_view(captured.message.data(), captured.message_size),
      "enabled=true");

  captured = {};
  MICROFMT_LOGGER_ERROR(application_logger,
                        MICROFMT_STRING("code={:02X}"), 0x2a);

  EXPECT_EQ(captured.count, 1U);
  EXPECT_EQ(captured.level, microfmt::log::level::err);
  EXPECT_EQ(
      std::string_view(captured.message.data(), captured.message_size),
      "code=2A");

  microfmt::log::set_default_logger(nullptr);
}

TEST(LoggerConfigurationTest, DefaultLoggerCanBeSetOrCleared) {
  captured = {};
  microfmt::log::set_default_logger(nullptr);
  EXPECT_EQ(microfmt::log::default_logger(), nullptr);

  microfmt::log::info("not emitted");
  EXPECT_EQ(captured.count, 0U);

  microfmt::log::set_default_logger(&application_logger);
  EXPECT_EQ(microfmt::log::default_logger(), &application_logger);

  microfmt::log::info("configured={}", true);
  EXPECT_EQ(captured.count, 1U);
  EXPECT_EQ(captured.level, microfmt::log::level::info);
  EXPECT_EQ(
      std::string_view(captured.message.data(), captured.message_size),
      "configured=true");

  microfmt::log::set_default_logger(nullptr);
}
