// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file log_msg.hpp @brief Structured log-record types and severity levels. */

#include "../microfmt.hpp"
#include <chrono>
#include <cstdint>
#include <source_location>
#include <string_view>

namespace microfmt::log {

// ============================================================================
// Severity Levels
// ============================================================================

/** @brief Shared log severity levels in ascending order. */
enum class level : uint8_t { trace = 0, debug, info, warn, err, critical, off };

[[nodiscard]] constexpr microfmt::string_view to_string_view(level lvl) noexcept {
  switch (lvl) {
  case level::trace:
    return "trace";
  case level::debug:
    return "debug";
  case level::info:
    return "info";
  case level::warn:
    return "warn";
  case level::err:
    return "error";
  case level::critical:
    return "critical";
  default:
    return "off";
  }
}

[[nodiscard]] constexpr microfmt::string_view to_short_string(level lvl) noexcept {
  switch (lvl) {
  case level::trace:
    return "T";
  case level::debug:
    return "D";
  case level::info:
    return "I";
  case level::warn:
    return "W";
  case level::err:
    return "E";
  case level::critical:
    return "C";
  default:
    return "O";
  }
}

// ============================================================================
// Log Entry Record (Carries zero-copy metadata)
// ============================================================================

/** @brief Structured log record forwarded by a logger to each sink. */
struct log_msg {
  microfmt::string_view logger_name{};
  level lvl{level::info};
  std::chrono::system_clock::time_point time{std::chrono::system_clock::now()};
  microfmt::string_view payload{};
  std::source_location loc{std::source_location::current()};
};

} // namespace microfmt::log
