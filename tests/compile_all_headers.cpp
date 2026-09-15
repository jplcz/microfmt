// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/detail/assert.hpp>
#include <microfmt/expected.hpp>
#include <microfmt/microfmt.hpp>
#include <microfmt/rvalue_safety.hpp>
#include <microfmt/string_view.hpp>
#include <microfmt/markdown.hpp>
#include <microfmt/formatters/ansi.hpp>
#include <microfmt/formatters/base_views.hpp>
#include <microfmt/formatters/binary.hpp>
#include <microfmt/formatters/bitfield.hpp>
#include <microfmt/formatters/can.hpp>
#include <microfmt/formatters/cbor.hpp>
#include <microfmt/formatters/chrono.hpp>
#include <microfmt/formatters/escaped.hpp>
#include <microfmt/formatters/filter_view.hpp>
#include <microfmt/formatters/fixed_point.hpp>
#include <microfmt/formatters/fmt.hpp>
#include <microfmt/formatters/format_helpers.hpp>
#include <microfmt/formatters/hexdump.hpp>
#include <microfmt/formatters/i2c.hpp>
#include <microfmt/formatters/json.hpp>
#include <microfmt/formatters/math.hpp>
#include <microfmt/formatters/map_view.hpp>
#include <microfmt/formatters/monad.hpp>
#include <microfmt/formatters/net.hpp>
#include <microfmt/formatters/pointer.hpp>
#include <microfmt/formatters/ranges.hpp>
#include <microfmt/formatters/semver.hpp>
#include <microfmt/formatters/spi.hpp>
#include <microfmt/formatters/source_location.hpp>
#include <microfmt/formatters/styled.hpp>
#include <microfmt/formatters/units.hpp>
#include <microfmt/formatters/uuid.hpp>
#include <microfmt/formatters/tuple.hpp>
#include <microfmt/sinks/ring_buffer_sink.hpp>
#include <microfmt/sinks/container_sink.hpp>
#include <microfmt/sinks/pmr_sink.hpp>
#include <microfmt/sinks/stdio.hpp>
#include <microfmt/sinks/styled_sink.hpp>
#include <microfmt/sinks/tee_sink.hpp>
#include <microfmt/inspector/address_translator.hpp>
#include <microfmt/inspector/memory_classifier.hpp>
#include <microfmt/inspector/memory_scanner.hpp>
#include <microfmt/inspector/register_view.hpp>

#if MICROFMT_HEADER_CHECK_STANDARD >= 20
#include <microfmt/log/logger.hpp>
#include <microfmt/log/macros.hpp>
#include <microfmt/sinks/android_log_sink.hpp>
#include <microfmt/sinks/syslog_sink.hpp>
#if defined(MICROFMT_COMPILE_WITH_SYSTEMD)
#include <microfmt/sinks/systemd_sink.hpp>
#endif
#if defined(MICROFMT_COMPILE_WITH_TIZEN_DLOG)
#include <microfmt/sinks/tizen_dlog_sink.hpp>
#endif
#endif

#if defined(MICROFMT_COMPILE_WITH_BOOST_DESCRIBE)
#include <microfmt/formatters/boost_describe.hpp>
#endif

#if MICROFMT_HEADER_CHECK_STANDARD == 17
static_assert(__cplusplus >= 201703L && __cplusplus < 202002L,
              "The C++17 header check must use C++17 mode.");
#elif MICROFMT_HEADER_CHECK_STANDARD == 20
static_assert(__cplusplus >= 202002L && __cplusplus < 202100L,
              "The C++20 header check must use C++20 mode.");
#elif MICROFMT_HEADER_CHECK_STANDARD == 23
static_assert(__cplusplus > 202002L,
              "The C++23 header check must use C++23 mode.");
#else
#error "MICROFMT_HEADER_CHECK_STANDARD must name the selected C++ standard."
#endif

#if MICROFMT_HAS_BOOST_SOURCE_LOCATION
namespace {
[[maybe_unused]] void verify_boost_source_location_formatter() {
  microfmt::buffer_sink<256> output;
  const auto location = BOOST_CURRENT_LOCATION;
  microfmt::format_to(output.as_sink(), "{} {}", location,
                      microfmt::source_loc(location));
}
} // namespace
#endif
