// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file macros.hpp @brief Source-location-aware structured logging macros. */

#include "logger.hpp"
#include <source_location>

// ============================================================================
// Targeted Logger Macros (MICROFMT_LOGGER_*)
// ============================================================================

#define MICROFMT_LOGGER_LOG(logger_instance, lvl, fmt, ...)                    \
  do {                                                                         \
    if ((logger_instance).should_log(lvl)) {                                   \
      (logger_instance)                                                        \
          .log_loc(::std::source_location::current(), lvl,                    \
                   fmt __VA_OPT__(, ) __VA_ARGS__);                            \
    }                                                                          \
  } while (0)

#define MICROFMT_LOGGER_TRACE(logger, fmt, ...)                                \
  MICROFMT_LOGGER_LOG(logger, ::microfmt::log::level::trace, fmt, __VA_ARGS__)
#define MICROFMT_LOGGER_DEBUG(logger, fmt, ...)                                \
  MICROFMT_LOGGER_LOG(logger, ::microfmt::log::level::debug, fmt, __VA_ARGS__)
#define MICROFMT_LOGGER_INFO(logger, fmt, ...)                                 \
  MICROFMT_LOGGER_LOG(logger, ::microfmt::log::level::info, fmt, __VA_ARGS__)
#define MICROFMT_LOGGER_WARN(logger, fmt, ...)                                 \
  MICROFMT_LOGGER_LOG(logger, ::microfmt::log::level::warn, fmt, __VA_ARGS__)
#define MICROFMT_LOGGER_ERROR(logger, fmt, ...)                                \
  MICROFMT_LOGGER_LOG(logger, ::microfmt::log::level::err, fmt, __VA_ARGS__)
#define MICROFMT_LOGGER_CRITICAL(logger, fmt, ...)                             \
  MICROFMT_LOGGER_LOG(logger, ::microfmt::log::level::critical, fmt,          \
                      __VA_ARGS__)

// ============================================================================
// Default Logger Macros (MICROFMT_LOG_*)
// ============================================================================

#if defined(MICROFMT_DEFAULT_LOGGER)

#define MICROFMT_LOG_TRACE(fmt, ...)                                          \
  MICROFMT_LOGGER_TRACE(MICROFMT_DEFAULT_LOGGER, fmt, __VA_ARGS__)
#define MICROFMT_LOG_DEBUG(fmt, ...)                                          \
  MICROFMT_LOGGER_DEBUG(MICROFMT_DEFAULT_LOGGER, fmt, __VA_ARGS__)
#define MICROFMT_LOG_INFO(fmt, ...)                                           \
  MICROFMT_LOGGER_INFO(MICROFMT_DEFAULT_LOGGER, fmt, __VA_ARGS__)
#define MICROFMT_LOG_WARN(fmt, ...)                                           \
  MICROFMT_LOGGER_WARN(MICROFMT_DEFAULT_LOGGER, fmt, __VA_ARGS__)
#define MICROFMT_LOG_ERROR(fmt, ...)                                          \
  MICROFMT_LOGGER_ERROR(MICROFMT_DEFAULT_LOGGER, fmt, __VA_ARGS__)
#define MICROFMT_LOG_CRITICAL(fmt, ...)                                       \
  MICROFMT_LOGGER_CRITICAL(MICROFMT_DEFAULT_LOGGER, fmt, __VA_ARGS__)

#elif defined(MICROFMT_ENABLE_DEFAULT_LOGGER)

#define MICROFMT_LOG_TRACE(fmt, ...)                                          \
  do {                                                                         \
    if (auto *microfmt_default_logger = ::microfmt::log::default_logger()) {  \
      MICROFMT_LOGGER_TRACE(*microfmt_default_logger, fmt, __VA_ARGS__);      \
    }                                                                          \
  } while (0)
#define MICROFMT_LOG_DEBUG(fmt, ...)                                          \
  do {                                                                         \
    if (auto *microfmt_default_logger = ::microfmt::log::default_logger()) {  \
      MICROFMT_LOGGER_DEBUG(*microfmt_default_logger, fmt, __VA_ARGS__);      \
    }                                                                          \
  } while (0)
#define MICROFMT_LOG_INFO(fmt, ...)                                           \
  do {                                                                         \
    if (auto *microfmt_default_logger = ::microfmt::log::default_logger()) {  \
      MICROFMT_LOGGER_INFO(*microfmt_default_logger, fmt, __VA_ARGS__);       \
    }                                                                          \
  } while (0)
#define MICROFMT_LOG_WARN(fmt, ...)                                           \
  do {                                                                         \
    if (auto *microfmt_default_logger = ::microfmt::log::default_logger()) {  \
      MICROFMT_LOGGER_WARN(*microfmt_default_logger, fmt, __VA_ARGS__);       \
    }                                                                          \
  } while (0)
#define MICROFMT_LOG_ERROR(fmt, ...)                                          \
  do {                                                                         \
    if (auto *microfmt_default_logger = ::microfmt::log::default_logger()) {  \
      MICROFMT_LOGGER_ERROR(*microfmt_default_logger, fmt, __VA_ARGS__);      \
    }                                                                          \
  } while (0)
#define MICROFMT_LOG_CRITICAL(fmt, ...)                                       \
  do {                                                                         \
    if (auto *microfmt_default_logger = ::microfmt::log::default_logger()) {  \
      MICROFMT_LOGGER_CRITICAL(*microfmt_default_logger, fmt, __VA_ARGS__);   \
    }                                                                          \
  } while (0)

#endif
