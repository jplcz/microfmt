// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file compat.hpp
 * @brief Compiler, language-version, and optional-library feature detection.
 *
 * Core compiler/language feature-detection macros (`RELOCO_CXX17`,
 * `RELOCO_HAS_STD_SPAN`, `RELOCO_NODISCARD`, `RELOCO_TRAP`, ...) are defined
 * by `reloco` (see `<reloco/detail/compat.hpp>`) and used directly; this
 * header only adds microfmt-specific feature detection reloco has no need
 * for (source_location, std::expected, Boost UUID, POSIX fd, Android log). */

#include "../microfmt_config.hpp"

#include <reloco/detail/compat.hpp>

#if RELOCO_CXX20 && RELOCO_HAS_INCLUDE(<source_location>)
#include <source_location>
#endif

#if RELOCO_CXX20 && defined(__cpp_lib_source_location) && (__cpp_lib_source_location >= 201907L)
#define MICROFMT_HAS_STD_SOURCE_LOCATION 1
#else
#define MICROFMT_HAS_STD_SOURCE_LOCATION 0
#endif

#if RELOCO_CXX23 && RELOCO_HAS_INCLUDE(<expected>)
#include <expected>
#endif

#if RELOCO_CXX23 && defined(__cpp_lib_expected) && (__cpp_lib_expected >= 202202L)
#define MICROFMT_HAS_STD_EXPECTED 1
#else
#define MICROFMT_HAS_STD_EXPECTED 0
#endif

#if defined(MICROFMT_ENABLE_BOOST_SOURCE_LOCATION)
// clang-format off
#if RELOCO_HAS_INCLUDE(<boost/assert/source_location.hpp>)
#include <boost/assert/source_location.hpp>
// clang-format on
#define MICROFMT_HAS_BOOST_SOURCE_LOCATION 1
#else
#error "MICROFMT_ENABLE_BOOST_SOURCE_LOCATION requires <boost/assert/source_location.hpp>"
#endif
#else
#define MICROFMT_HAS_BOOST_SOURCE_LOCATION 0
#endif

#if MICROFMT_HAS_STD_SOURCE_LOCATION || MICROFMT_HAS_BOOST_SOURCE_LOCATION
#define MICROFMT_HAS_SOURCE_LOCATION 1
#else
#define MICROFMT_HAS_SOURCE_LOCATION 0
#endif

#if defined(MICROFMT_ENABLE_BOOST_UUID) || defined(BOOST_UUID_UUID_HPP) || defined(BOOST_UUID_HPP)
#if !defined(BOOST_UUID_UUID_HPP) && !defined(BOOST_UUID_HPP)
// clang-format off
#if RELOCO_HAS_INCLUDE(<boost/uuid/uuid.hpp>)
#include <boost/uuid/uuid.hpp>
// clang-format on
#else
#error "MICROFMT_ENABLE_BOOST_UUID requires <boost/uuid/uuid.hpp>"
#endif
#endif
#define MICROFMT_HAS_BOOST_UUID 1
#else
#define MICROFMT_HAS_BOOST_UUID 0
#endif

#if defined(__unix__) || defined(__APPLE__) || defined(__posix)
#define MICROFMT_HAS_POSIX_FD 1
#else
#define MICROFMT_HAS_POSIX_FD 0
#endif

#if defined(__ANDROID__)
#define MICROFMT_HAS_ANDROID_LOG 1
#else
#define MICROFMT_HAS_ANDROID_LOG 0
#endif
