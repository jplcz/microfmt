// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file win_debug_sink.hpp
 * @brief Sink and log adapter for the Windows debugger output channel
 * (`OutputDebugStringA`).
 *
 * Use it on Windows to see output in the Visual Studio "Output" window or in
 * tools such as DebugView, even for GUI programs that have no console.
 * `OutputDebugStringA` needs a NUL-terminated string, so the sink copies
 * data into a fixed buffer and sends it in chunks of at most `Capacity - 1`
 * bytes.
 */

#include "../log/detail/record_line.hpp"
#include "../log/sink.hpp"
#include "../microfmt.hpp"
#include <cstddef>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace microfmt {

/** @brief Writes formatted output to the Windows debugger via a NUL-terminated chunk function. */
template <std::size_t Capacity = 512> class win_debug_sink {
  static_assert(Capacity > 1, "Capacity must hold at least one character and the terminating NUL");

public:
  /** @brief Receives one NUL-terminated chunk; `OutputDebugStringA` on Windows. */
  using write_fn_t = void (*)(const char *nul_terminated) noexcept;

#if defined(_WIN32)
  constexpr win_debug_sink() noexcept : write_fn_(&write_to_debugger) {}
#endif

  /** @brief Constructs a sink with a custom chunk function (used by tests and non-Windows hosts). */
  explicit constexpr win_debug_sink(write_fn_t write_fn) noexcept : write_fn_(write_fn) {}

  /** @brief Returns a type-erased `microfmt::sink` view over this instance. */
  [[nodiscard]] sink as_sink() & noexcept RELOCO_LIFETIMEBOUND {
    return sink{this, [](void *ctx, string_view sv) noexcept { static_cast<win_debug_sink *>(ctx)->write(sv); }};
  }

  /** @brief Sends @p data in NUL-terminated chunks of at most `Capacity - 1` bytes. */
  void write(string_view data) noexcept {
    while (!data.empty()) {
      const std::size_t n = data.size() < Capacity - 1 ? data.size() : Capacity - 1;
      buffer_sink<Capacity> chunk;
      const auto out = chunk.as_sink();
      out.write(data.substr(0, n));
      out.write(string_view("\0", 1));
      write_fn_(chunk.view().data());
      data.remove_prefix(n);
    }
  }

private:
#if defined(_WIN32)
  static void write_to_debugger(const char *text) noexcept { ::OutputDebugStringA(text); }
#endif

  write_fn_t write_fn_;
};

#if defined(_WIN32)
win_debug_sink() -> win_debug_sink<>;
#endif

} // namespace microfmt

namespace microfmt::log {

template <std::size_t Capacity> class win_debug_log_sink;

/** @brief Tag selecting `win_debug_log_sink<Capacity>` as a `log_sink` backend. */
template <std::size_t Capacity> struct win_debug_log_sink_tag {};

/** @brief Log adapter that sends each record as one `[logger] payload\n` line to the debugger. */
template <std::size_t Capacity = 512> class win_debug_log_sink {
  static_assert(Capacity > 2, "Capacity must hold the record, newline and NUL");

public:
  explicit win_debug_log_sink(const win_debug_sink<Capacity> &target) noexcept : target_(target) {}

  [[nodiscard]] log_sink as_sink() & noexcept RELOCO_LIFETIMEBOUND {
    return log_sink(win_debug_log_sink_tag<Capacity>{}, *this);
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
  win_debug_sink<Capacity> target_;
};

template <std::size_t Capacity> struct log_sink_traits<win_debug_log_sink_tag<Capacity>> {
  using context_type = win_debug_log_sink<Capacity>;

  static void log(value_ref<context_type> ctx, const log_msg &msg) noexcept { ctx->log_impl(msg); }
};

} // namespace microfmt::log
