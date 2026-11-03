// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <cstddef>

#include <microfmt/log/logger.hpp>

namespace {

std::size_t sink_count = 0;

struct counting_tag {};

} // namespace

namespace microfmt::log {
template <> struct log_sink_traits<counting_tag> {
  using context_type = void;

  static void log(const log_msg &) noexcept { ++sink_count; }
};
} // namespace microfmt::log

// Tests must see WARN regardless of a project-wide JPLCZ_MICROFMT_ACTIVE_LEVEL.
#undef MICROFMT_ACTIVE_LEVEL
#define MICROFMT_ACTIVE_LEVEL 3
#define MICROFMT_DEFAULT_LOGGER active_level_logger
namespace {
microfmt::log::logger active_level_logger("active_level",
                                          microfmt::log::log_sink(counting_tag{}, microfmt::log::level::trace));
} // namespace
#include <microfmt/log/macros.hpp>

TEST(LogActiveLevelTest, MacrosBelowActiveLevelAreNotEvaluated) {
  sink_count = 0;
  int evaluated = 0;

  MICROFMT_LOGGER_TRACE(active_level_logger, "{}", ++evaluated);
  MICROFMT_LOGGER_DEBUG(active_level_logger, "{}", ++evaluated);
  MICROFMT_LOGGER_INFO(active_level_logger, "{}", ++evaluated);
  MICROFMT_LOG_INFO("{}", ++evaluated);

  EXPECT_EQ(evaluated, 0);
  EXPECT_EQ(sink_count, 0U);
}

TEST(LogActiveLevelTest, MacrosAtOrAboveActiveLevelStillLog) {
  sink_count = 0;
  int evaluated = 0;

  MICROFMT_LOGGER_WARN(active_level_logger, "{}", ++evaluated);
  MICROFMT_LOG_ERROR("{}", ++evaluated);
  MICROFMT_LOGGER_CRITICAL(active_level_logger, "{}", ++evaluated);

  EXPECT_EQ(evaluated, 3);
  EXPECT_EQ(sink_count, 3U);
}

TEST(LogActiveLevelTest, DisabledMacroIsSafeInUnbracedIfElse) {
  bool else_taken = false;
  if (sink_count == static_cast<std::size_t>(-1))
    MICROFMT_LOGGER_INFO(active_level_logger, "x");
  else
    else_taken = true;
  EXPECT_TRUE(else_taken);
}
