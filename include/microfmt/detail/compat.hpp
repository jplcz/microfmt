// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file compat.hpp
 * @brief Compiler, language-version, and optional-library feature detection. */

#include <cstdlib>

#if defined(_MSVC_LANG)
#define MICROFMT_CXX_STANDARD _MSVC_LANG
#else
#define MICROFMT_CXX_STANDARD __cplusplus
#endif

#define MICROFMT_CXX11 (MICROFMT_CXX_STANDARD >= 201103L)
#define MICROFMT_CXX14 (MICROFMT_CXX_STANDARD >= 201402L)
#define MICROFMT_CXX17 (MICROFMT_CXX_STANDARD >= 201703L)
#define MICROFMT_CXX20 (MICROFMT_CXX_STANDARD >= 202002L)
#define MICROFMT_CXX23 (MICROFMT_CXX_STANDARD > 202002L)

#if defined(__has_include)
#define MICROFMT_HAS_INCLUDE(header) __has_include(header)
#else
#define MICROFMT_HAS_INCLUDE(header) 0
#endif

#if defined(__has_cpp_attribute)
#define MICROFMT_HAS_CPP_ATTRIBUTE(attribute) __has_cpp_attribute(attribute)
#else
#define MICROFMT_HAS_CPP_ATTRIBUTE(attribute) 0
#endif

#if defined(__has_attribute)
#define MICROFMT_HAS_ATTRIBUTE(attribute) __has_attribute(attribute)
#else
#define MICROFMT_HAS_ATTRIBUTE(attribute) 0
#endif

#if MICROFMT_CXX17 && MICROFMT_HAS_CPP_ATTRIBUTE(nodiscard)
#define MICROFMT_NODISCARD [[nodiscard]]
#else
#define MICROFMT_NODISCARD
#endif

#if MICROFMT_CXX17 && MICROFMT_HAS_CPP_ATTRIBUTE(maybe_unused)
#define MICROFMT_MAYBE_UNUSED [[maybe_unused]]
#else
#define MICROFMT_MAYBE_UNUSED
#endif

#if MICROFMT_CXX20 && MICROFMT_HAS_CPP_ATTRIBUTE(no_unique_address)
#define MICROFMT_NO_UNIQUE_ADDRESS [[no_unique_address]]
#elif defined(_MSC_VER) && MICROFMT_HAS_CPP_ATTRIBUTE(msvc::no_unique_address)
#define MICROFMT_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define MICROFMT_NO_UNIQUE_ADDRESS
#endif

#if MICROFMT_CXX20 && MICROFMT_HAS_CPP_ATTRIBUTE(likely)
#define MICROFMT_LIKELY [[likely]]
#define MICROFMT_UNLIKELY [[unlikely]]
#else
#define MICROFMT_LIKELY
#define MICROFMT_UNLIKELY
#endif

#if defined(__clang__) || defined(__GNUC__)
#define MICROFMT_ALWAYS_INLINE __attribute__((always_inline))
#elif defined(_MSC_VER)
#include <intrin.h>
#define MICROFMT_ALWAYS_INLINE
#else
#define MICROFMT_ALWAYS_INLINE
#endif

#if !defined(MICROFMT_TRAP)
#if defined(__clang__) || defined(__GNUC__)
#define MICROFMT_TRAP() __builtin_trap()
#elif defined(_MSC_VER)
#define MICROFMT_TRAP() __debugbreak()
#else
#define MICROFMT_TRAP() std::abort()
#endif
#endif

#if !defined(MICROFMT_UNREACHABLE)
#if defined(__clang__) || defined(__GNUC__)
#define MICROFMT_UNREACHABLE() __builtin_unreachable()
#if !defined(MICROFMT_HAS_UNREACHABLE)
#define MICROFMT_HAS_UNREACHABLE 1
#endif
#elif defined(_MSC_VER)
#define MICROFMT_UNREACHABLE() __assume(0)
#if !defined(MICROFMT_HAS_UNREACHABLE)
#define MICROFMT_HAS_UNREACHABLE 1
#endif
#else
#define MICROFMT_UNREACHABLE() ((void)0)
#if !defined(MICROFMT_HAS_UNREACHABLE)
#define MICROFMT_HAS_UNREACHABLE 0
#endif
#endif
#elif !defined(MICROFMT_HAS_UNREACHABLE)
#define MICROFMT_HAS_UNREACHABLE 1
#endif

#if MICROFMT_CXX20 && MICROFMT_HAS_INCLUDE(<span>)
#include <span>
#define MICROFMT_HAS_STD_SPAN 1
#else
#define MICROFMT_HAS_STD_SPAN 0
#endif

#if MICROFMT_CXX20 && MICROFMT_HAS_INCLUDE(<source_location>)
#include <source_location>
#endif

#if MICROFMT_CXX20 && defined(__cpp_lib_source_location) && (__cpp_lib_source_location >= 201907L)
#define MICROFMT_HAS_STD_SOURCE_LOCATION 1
#else
#define MICROFMT_HAS_STD_SOURCE_LOCATION 0
#endif

#if MICROFMT_CXX23 && MICROFMT_HAS_INCLUDE(<expected>)
#include <expected>
#endif

#if MICROFMT_CXX23 && defined(__cpp_lib_expected) && (__cpp_lib_expected >= 202202L)
#define MICROFMT_HAS_STD_EXPECTED 1
#else
#define MICROFMT_HAS_STD_EXPECTED 0
#endif

#if defined(MICROFMT_ENABLE_BOOST_SOURCE_LOCATION)
#if MICROFMT_HAS_INCLUDE(<boost/assert/source_location.hpp>)
#include <boost/assert/source_location.hpp>
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
#if MICROFMT_HAS_INCLUDE(<boost/uuid/uuid.hpp>)
#include <boost/uuid/uuid.hpp>
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
