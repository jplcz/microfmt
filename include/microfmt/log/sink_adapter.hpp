// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file sink_adapter.hpp
 * @brief Turns any byte-oriented `microfmt::sink` into a `log_sink` backend.
 *
 * Byte sinks (debug-probe channels, UARTs, packet writers) know nothing about
 * log records. `sink_log_adapter` renders each record as one
 * `[logger] payload\n` line into a fixed buffer and writes it to the borrowed
 * sink in a single call, so lines from different loggers are not interleaved
 * mid-record by the adapter itself.
 */

#include "../microfmt.hpp"
#include "detail/record_line.hpp"
#include "sink.hpp"
#include <cstddef>

namespace microfmt::log {

template <std::size_t Capacity> class sink_log_adapter;

/** @brief Tag selecting `sink_log_adapter<Capacity>` as a `log_sink` backend. */
template <std::size_t Capacity> struct sink_log_adapter_tag {};

/** @brief Log backend that formats one line per record and writes it to a borrowed `microfmt::sink`. */
template <std::size_t Capacity = 256> class sink_log_adapter {
  static_assert(Capacity > 1, "Capacity must leave room for the trailing newline");

public:
  /** @brief Borrows @p target; the object behind it must outlive this adapter. */
  explicit sink_log_adapter(sink target) noexcept : target_(target) {}

  [[nodiscard]] log_sink as_sink() & noexcept RELOCO_LIFETIMEBOUND {
    return log_sink(sink_log_adapter_tag<Capacity>{}, *this);
  }

  void log_impl(const log_msg &msg) noexcept {
    if (msg.lvl == level::off) {
      return;
    }
    buffer_sink<Capacity> buffer;
    detail::format_record_line(buffer, msg, true);
    target_.write(buffer.view());
  }

private:
  sink target_;
};

template <std::size_t Capacity> struct log_sink_traits<sink_log_adapter_tag<Capacity>> {
  using context_type = sink_log_adapter<Capacity>;

  static void log(value_ref<context_type> ctx, const log_msg &msg) noexcept { ctx->log_impl(msg); }
};

} // namespace microfmt::log
