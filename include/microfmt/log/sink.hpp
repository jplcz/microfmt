#pragma once

/** @file sink.hpp @brief Type-erased structured logging sinks and console output. */

#include "../formatters/ansi.hpp"
#include "../formatters/chrono.hpp"
#include "../sinks/stdio.hpp"
#include "log_msg.hpp"
#include <cstddef>
#include <string_view>

namespace microfmt::log {

// ============================================================================
// Abstract Log Sink Interface (Polymorphism via Type-Erased Function Pointer)
// ============================================================================

struct log_sink {
  void *ctx{nullptr};
  void (*log_fn)(void *ctx, const log_msg &msg) noexcept {nullptr};
  void (*flush_fn)(void *ctx) noexcept {nullptr};
  level lvl{level::trace};

  void log(const log_msg &msg) noexcept {
    if (msg.lvl >= lvl && log_fn) {
      log_fn(ctx, msg);
    }
  }

  void flush() noexcept {
    if (flush_fn) {
      flush_fn(ctx);
    }
  }

  void set_level(level l) noexcept { lvl = l; }
  [[nodiscard]] bool should_log(level l) const noexcept { return l >= lvl; }
};

// ============================================================================
// Built-in Standard Sinks
// ============================================================================

// ANSI Color Console Sink (stdout / stderr)
template <size_t LineBufCap = 256> class stdout_color_sink {
public:
  stdout_color_sink() noexcept = default;

  [[nodiscard]] log_sink as_sink() noexcept {
    return log_sink{this,
                    [](void *ctx, const log_msg &msg) noexcept {
                      static_cast<stdout_color_sink *>(ctx)->log_impl(msg);
                    },
                    [](void *ctx) noexcept {
                      static_cast<stdout_color_sink *>(ctx)->flush_impl();
                    },
                    level::trace};
  }

private:
  void log_impl(const log_msg &msg) noexcept {
    buffer_sink<LineBufCap> buf;
    auto out = buf.as_sink();

    // Timestamp: [HH:MM:SS.mmm]
    format_to(out, "[{:t}] ", msg.time);

    // Logger Tag
    if (!msg.logger_name.empty()) {
      format_to(out, "[{}] ", msg.logger_name);
    }

    // Colorized Level Tag
    switch (msg.lvl) {
    case level::trace:
      format_to(out, "[{}] ", ansi::gray("TRACE"));
      break;
    case level::debug:
      format_to(out, "[{}] ", ansi::cyan("DEBUG"));
      break;
    case level::info:
      format_to(out, "[{}] ", ansi::green("INFO "));
      break;
    case level::warn:
      format_to(out, "[{}] ", ansi::yellow("WARN "));
      break;
    case level::err:
      format_to(out, "[{}] ", ansi::red("ERROR"));
      break;
    case level::critical:
      format_to(out, "[{}] ", ansi::styled(
                                "CRIT ",
                                ansi::style{ansi::color::bright_white,
                                            ansi::color::red,
                                            ansi::attribute::bold}));
      break;
    default:
      break;
    }

    // Message payload + newline
    format_to(out, "{}\n", msg.payload);

    // Output to direct stdout file descriptor
    auto term = stdout_sink();
    term.write(buf.view());
  }

  void flush_impl() noexcept {}
};

} // namespace microfmt::log