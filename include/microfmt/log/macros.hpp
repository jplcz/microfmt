// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file macros.hpp @brief Source-location-aware structured logging macros. */

#include "logger.hpp"
#include <source_location>

// ============================================================================
// Compile-time level (MICROFMT_ACTIVE_LEVEL)
// ============================================================================
//
// Level macros below MICROFMT_ACTIVE_LEVEL expand to ((void)0): arguments are
// not evaluated and neither the format string nor the source_location data
// reach the object file. Defaults to MICROFMT_LEVEL_TRACE (everything kept).
// The run-time should_log() filter still applies to the remaining levels.

#define MICROFMT_LEVEL_TRACE 0
#define MICROFMT_LEVEL_DEBUG 1
#define MICROFMT_LEVEL_INFO 2
#define MICROFMT_LEVEL_WARN 3
#define MICROFMT_LEVEL_ERROR 4
#define MICROFMT_LEVEL_CRITICAL 5
#define MICROFMT_LEVEL_OFF 6

#ifndef MICROFMT_ACTIVE_LEVEL
#define MICROFMT_ACTIVE_LEVEL MICROFMT_LEVEL_TRACE
#endif

// ============================================================================
// Targeted Logger Macros (MICROFMT_LOGGER_*)
// ============================================================================

#define MICROFMT_LOGGER_LOG(logger_instance, lvl, fmt, ...)                                                            \
  do {                                                                                                                 \
    if ((logger_instance).should_log(lvl)) {                                                                           \
      (logger_instance).log_loc(::std::source_location::current(), lvl, fmt __VA_OPT__(, ) __VA_ARGS__);               \
    }                                                                                                                  \
  } while (0)

#define MICROFMT_LOGGER_TRACE(logger, fmt, ...)                                                                        \
  MICROFMT_LOGGER_LOG(logger, ::microfmt::log::level::trace, fmt, __VA_ARGS__)
#define MICROFMT_LOGGER_DEBUG(logger, fmt, ...)                                                                        \
  MICROFMT_LOGGER_LOG(logger, ::microfmt::log::level::debug, fmt, __VA_ARGS__)
#define MICROFMT_LOGGER_INFO(logger, fmt, ...)                                                                         \
  MICROFMT_LOGGER_LOG(logger, ::microfmt::log::level::info, fmt, __VA_ARGS__)
#define MICROFMT_LOGGER_WARN(logger, fmt, ...)                                                                         \
  MICROFMT_LOGGER_LOG(logger, ::microfmt::log::level::warn, fmt, __VA_ARGS__)
#define MICROFMT_LOGGER_ERROR(logger, fmt, ...)                                                                        \
  MICROFMT_LOGGER_LOG(logger, ::microfmt::log::level::err, fmt, __VA_ARGS__)
#define MICROFMT_LOGGER_CRITICAL(logger, fmt, ...)                                                                     \
  MICROFMT_LOGGER_LOG(logger, ::microfmt::log::level::critical, fmt, __VA_ARGS__)

// ============================================================================
// Default Logger Macros (MICROFMT_LOG_*)
// ============================================================================

#if defined(MICROFMT_DEFAULT_LOGGER)

#define MICROFMT_LOG_TRACE(fmt, ...) MICROFMT_LOGGER_TRACE(MICROFMT_DEFAULT_LOGGER, fmt, __VA_ARGS__)
#define MICROFMT_LOG_DEBUG(fmt, ...) MICROFMT_LOGGER_DEBUG(MICROFMT_DEFAULT_LOGGER, fmt, __VA_ARGS__)
#define MICROFMT_LOG_INFO(fmt, ...) MICROFMT_LOGGER_INFO(MICROFMT_DEFAULT_LOGGER, fmt, __VA_ARGS__)
#define MICROFMT_LOG_WARN(fmt, ...) MICROFMT_LOGGER_WARN(MICROFMT_DEFAULT_LOGGER, fmt, __VA_ARGS__)
#define MICROFMT_LOG_ERROR(fmt, ...) MICROFMT_LOGGER_ERROR(MICROFMT_DEFAULT_LOGGER, fmt, __VA_ARGS__)
#define MICROFMT_LOG_CRITICAL(fmt, ...) MICROFMT_LOGGER_CRITICAL(MICROFMT_DEFAULT_LOGGER, fmt, __VA_ARGS__)

#elif defined(MICROFMT_ENABLE_DEFAULT_LOGGER)

#define MICROFMT_LOG_TRACE(fmt, ...)                                                                                   \
  do {                                                                                                                 \
    if (auto *microfmt_default_logger = ::microfmt::log::default_logger()) {                                           \
      MICROFMT_LOGGER_TRACE(*microfmt_default_logger, fmt, __VA_ARGS__);                                               \
    }                                                                                                                  \
  } while (0)
#define MICROFMT_LOG_DEBUG(fmt, ...)                                                                                   \
  do {                                                                                                                 \
    if (auto *microfmt_default_logger = ::microfmt::log::default_logger()) {                                           \
      MICROFMT_LOGGER_DEBUG(*microfmt_default_logger, fmt, __VA_ARGS__);                                               \
    }                                                                                                                  \
  } while (0)
#define MICROFMT_LOG_INFO(fmt, ...)                                                                                    \
  do {                                                                                                                 \
    if (auto *microfmt_default_logger = ::microfmt::log::default_logger()) {                                           \
      MICROFMT_LOGGER_INFO(*microfmt_default_logger, fmt, __VA_ARGS__);                                                \
    }                                                                                                                  \
  } while (0)
#define MICROFMT_LOG_WARN(fmt, ...)                                                                                    \
  do {                                                                                                                 \
    if (auto *microfmt_default_logger = ::microfmt::log::default_logger()) {                                           \
      MICROFMT_LOGGER_WARN(*microfmt_default_logger, fmt, __VA_ARGS__);                                                \
    }                                                                                                                  \
  } while (0)
#define MICROFMT_LOG_ERROR(fmt, ...)                                                                                   \
  do {                                                                                                                 \
    if (auto *microfmt_default_logger = ::microfmt::log::default_logger()) {                                           \
      MICROFMT_LOGGER_ERROR(*microfmt_default_logger, fmt, __VA_ARGS__);                                               \
    }                                                                                                                  \
  } while (0)
#define MICROFMT_LOG_CRITICAL(fmt, ...)                                                                                \
  do {                                                                                                                 \
    if (auto *microfmt_default_logger = ::microfmt::log::default_logger()) {                                           \
      MICROFMT_LOGGER_CRITICAL(*microfmt_default_logger, fmt, __VA_ARGS__);                                            \
    }                                                                                                                  \
  } while (0)

#endif

// ============================================================================
// Compile-time disabling
// ============================================================================

#if MICROFMT_ACTIVE_LEVEL > MICROFMT_LEVEL_TRACE
#undef MICROFMT_LOGGER_TRACE
#define MICROFMT_LOGGER_TRACE(logger, fmt, ...) ((void)0)
#ifdef MICROFMT_LOG_TRACE
#undef MICROFMT_LOG_TRACE
#define MICROFMT_LOG_TRACE(fmt, ...) ((void)0)
#endif
#endif

#if MICROFMT_ACTIVE_LEVEL > MICROFMT_LEVEL_DEBUG
#undef MICROFMT_LOGGER_DEBUG
#define MICROFMT_LOGGER_DEBUG(logger, fmt, ...) ((void)0)
#ifdef MICROFMT_LOG_DEBUG
#undef MICROFMT_LOG_DEBUG
#define MICROFMT_LOG_DEBUG(fmt, ...) ((void)0)
#endif
#endif

#if MICROFMT_ACTIVE_LEVEL > MICROFMT_LEVEL_INFO
#undef MICROFMT_LOGGER_INFO
#define MICROFMT_LOGGER_INFO(logger, fmt, ...) ((void)0)
#ifdef MICROFMT_LOG_INFO
#undef MICROFMT_LOG_INFO
#define MICROFMT_LOG_INFO(fmt, ...) ((void)0)
#endif
#endif

#if MICROFMT_ACTIVE_LEVEL > MICROFMT_LEVEL_WARN
#undef MICROFMT_LOGGER_WARN
#define MICROFMT_LOGGER_WARN(logger, fmt, ...) ((void)0)
#ifdef MICROFMT_LOG_WARN
#undef MICROFMT_LOG_WARN
#define MICROFMT_LOG_WARN(fmt, ...) ((void)0)
#endif
#endif

#if MICROFMT_ACTIVE_LEVEL > MICROFMT_LEVEL_ERROR
#undef MICROFMT_LOGGER_ERROR
#define MICROFMT_LOGGER_ERROR(logger, fmt, ...) ((void)0)
#ifdef MICROFMT_LOG_ERROR
#undef MICROFMT_LOG_ERROR
#define MICROFMT_LOG_ERROR(fmt, ...) ((void)0)
#endif
#endif

#if MICROFMT_ACTIVE_LEVEL > MICROFMT_LEVEL_CRITICAL
#undef MICROFMT_LOGGER_CRITICAL
#define MICROFMT_LOGGER_CRITICAL(logger, fmt, ...) ((void)0)
#ifdef MICROFMT_LOG_CRITICAL
#undef MICROFMT_LOG_CRITICAL
#define MICROFMT_LOG_CRITICAL(fmt, ...) ((void)0)
#endif
#endif
