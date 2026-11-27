// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file record_line.hpp
 * @brief Shared "[logger] payload\n" rendering for log adapters that send a
 * whole record to a byte transport (file descriptor, socket, debugger).
 */

#include "../../microfmt.hpp"
#include "../log_msg.hpp"
#include <cstddef>

namespace microfmt::log::detail {

/**
 * @brief Renders @p msg as `[logger] payload` (plus a trailing newline when
 * @p newline is true) into a fixed-capacity buffer, truncating on overflow.
 *
 * Keeping the record in one buffer lets datagram transports emit it as a
 * single packet, and avoids interleaving partial lines from several writers.
 */
template <std::size_t Capacity>
void format_record_line(buffer_sink<Capacity> &buffer, const log_msg &msg, bool newline) noexcept {
  const auto out = buffer.as_sink();
  if (!msg.logger_name.empty()) {
    microfmt::format_to(out, "[{}] ", msg.logger_name);
  }
  microfmt::format_to(out, "{}", msg.payload);
  if (newline) {
    out.write("\n");
  }
}

} // namespace microfmt::log::detail
