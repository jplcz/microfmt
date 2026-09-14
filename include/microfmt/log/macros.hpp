// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

/** @file macros.hpp @brief Source-location-aware structured logging macros. */

#include "logger.hpp"
#include <source_location>

// ============================================================================
// Targeted Logger Macros (MICROFMT_LOGGER_*)
// ============================================================================

#define MICROFMT_LOGGER_LOG(logger_instance, lvl, ...)                         \
  do {                                                                         \
    if ((logger_instance).should_log(lvl)) {                                   \
      (logger_instance)                                                        \
          .log_loc(::std::source_location::current(), lvl, __VA_ARGS__);       \
    }                                                                          \
  } while (0)

#define MICROFMT_LOGGER_TRACE(logger, ...)                                     \
  MICROFMT_LOGGER_LOG(logger, ::microfmt::log::level::trace, __VA_ARGS__)
#define MICROFMT_LOGGER_DEBUG(logger, ...)                                     \
  MICROFMT_LOGGER_LOG(logger, ::microfmt::log::level::debug, __VA_ARGS__)
#define MICROFMT_LOGGER_INFO(logger, ...)                                      \
  MICROFMT_LOGGER_LOG(logger, ::microfmt::log::level::info, __VA_ARGS__)
#define MICROFMT_LOGGER_WARN(logger, ...)                                      \
  MICROFMT_LOGGER_LOG(logger, ::microfmt::log::level::warn, __VA_ARGS__)
#define MICROFMT_LOGGER_ERROR(logger, ...)                                     \
  MICROFMT_LOGGER_LOG(logger, ::microfmt::log::level::err, __VA_ARGS__)
#define MICROFMT_LOGGER_CRITICAL(logger, ...)                                  \
  MICROFMT_LOGGER_LOG(logger, ::microfmt::log::level::critical, __VA_ARGS__)

// ============================================================================
// Default Logger Macros (MICROFMT_LOG_*)
// ============================================================================

#if defined(MICROFMT_DEFAULT_LOGGER)

#define MICROFMT_LOG_TRACE(...)                                                \
  MICROFMT_LOGGER_TRACE(MICROFMT_DEFAULT_LOGGER, __VA_ARGS__)
#define MICROFMT_LOG_DEBUG(...)                                                \
  MICROFMT_LOGGER_DEBUG(MICROFMT_DEFAULT_LOGGER, __VA_ARGS__)
#define MICROFMT_LOG_INFO(...)                                                 \
  MICROFMT_LOGGER_INFO(MICROFMT_DEFAULT_LOGGER, __VA_ARGS__)
#define MICROFMT_LOG_WARN(...)                                                 \
  MICROFMT_LOGGER_WARN(MICROFMT_DEFAULT_LOGGER, __VA_ARGS__)
#define MICROFMT_LOG_ERROR(...)                                                \
  MICROFMT_LOGGER_ERROR(MICROFMT_DEFAULT_LOGGER, __VA_ARGS__)
#define MICROFMT_LOG_CRITICAL(...)                                             \
  MICROFMT_LOGGER_CRITICAL(MICROFMT_DEFAULT_LOGGER, __VA_ARGS__)

#elif defined(MICROFMT_ENABLE_DEFAULT_LOGGER)

#define MICROFMT_LOG_TRACE(...)                                                \
  do {                                                                         \
    if (auto *microfmt_default_logger = ::microfmt::log::default_logger()) {  \
      MICROFMT_LOGGER_TRACE(*microfmt_default_logger, __VA_ARGS__);           \
    }                                                                          \
  } while (0)
#define MICROFMT_LOG_DEBUG(...)                                                \
  do {                                                                         \
    if (auto *microfmt_default_logger = ::microfmt::log::default_logger()) {  \
      MICROFMT_LOGGER_DEBUG(*microfmt_default_logger, __VA_ARGS__);           \
    }                                                                          \
  } while (0)
#define MICROFMT_LOG_INFO(...)                                                 \
  do {                                                                         \
    if (auto *microfmt_default_logger = ::microfmt::log::default_logger()) {  \
      MICROFMT_LOGGER_INFO(*microfmt_default_logger, __VA_ARGS__);            \
    }                                                                          \
  } while (0)
#define MICROFMT_LOG_WARN(...)                                                 \
  do {                                                                         \
    if (auto *microfmt_default_logger = ::microfmt::log::default_logger()) {  \
      MICROFMT_LOGGER_WARN(*microfmt_default_logger, __VA_ARGS__);            \
    }                                                                          \
  } while (0)
#define MICROFMT_LOG_ERROR(...)                                                \
  do {                                                                         \
    if (auto *microfmt_default_logger = ::microfmt::log::default_logger()) {  \
      MICROFMT_LOGGER_ERROR(*microfmt_default_logger, __VA_ARGS__);           \
    }                                                                          \
  } while (0)
#define MICROFMT_LOG_CRITICAL(...)                                             \
  do {                                                                         \
    if (auto *microfmt_default_logger = ::microfmt::log::default_logger()) {  \
      MICROFMT_LOGGER_CRITICAL(*microfmt_default_logger, __VA_ARGS__);        \
    }                                                                          \
  } while (0)

#endif
