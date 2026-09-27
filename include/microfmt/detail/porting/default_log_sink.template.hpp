// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file default_log_sink.template.hpp
 * @brief Documentation-only scaffold for
 * `MICROFMT_DEFAULT_LOG_SINK_BACKEND_CUSTOM`.
 *
 * Never `#include`d by anything -- copy this file to
 * `detail/porting/default_log_sink.hpp` (dropping `.template`), fill it in
 * for your actual target, and define
 * `MICROFMT_DEFAULT_LOG_SINK_BACKEND_CUSTOM` (see
 * `microfmt/log/logger.hpp`/`microfmt/microfmt_config.hpp`), or point the
 * `JPLCZ_MICROFMT_PORTING_HEADERS` CMake variable at a directory
 * containing your finished `default_log_sink.hpp` and let the build do
 * both for you.
 *
 * Sketches, loosely, what a **FreeBSD kernel** port might look like,
 * backed by `log(9)` (the kernel's own syslog-priority-tagged logging
 * facility, draining to `/var/log/messages` via syslogd and to the
 * console when above the console log level -- see `sys/syslog.h`/
 * `sys/systm.h`) instead of the built-in userspace `stdout_color_sink`.
 * This is illustrative, not exact or complete -- a real port has its own
 * conventions for which subsystem/facility to tag records with (`log()`
 * itself takes no separate facility argument beyond the `LOG_*` priority
 * baked into the level mapping below), and is not compiled or exercised
 * by this repository (which targets hosted userspace, not the FreeBSD
 * kernel proper). See `microfmt::log::syslog_sink`
 * (`microfmt/sinks/syslog_sink.hpp`) for this same idea's userspace
 * counterpart, which this scaffold otherwise mirrors closely.
 *
 * Defines the whole `microfmt::log::detail::built_in_default_logger()
 * noexcept -> logger &` entry point (not merely a sink helper), giving
 * this port full control over the resulting `logger` too -- its name,
 * sink count/capacity, locking policy -- exactly as the built-in
 * `stdout_color_sink`-backed definition this replaces does.
 *
 * Deliberately does not `#include` any of microfmt's own headers back
 * (e.g. `log/sink.hpp` for `log_sink`/`log_msg`/`level`/`buffer_sink`):
 * this file is spliced in by `logger.hpp` only after already including
 * them itself, so they are always in scope by the time this file is
 * reached, exactly like `mutex.template.hpp` relies on `RELOCO_CAPABILITY`
 * etc. already being visible from its own inclusion context.
 */

#include <sys/syslog.h>
#include <sys/systm.h>

namespace microfmt::log {

// Forward-declared so log_sink_traits<kernel_log_sink_tag> below can name
// it as context_type before it is complete. Unlike microfmt::sinks::
// syslog_sink's own tag/traits (a class *template*, whose as_sink() body
// is only instantiated -- and therefore only needs the traits visible --
// at first use, by which point the whole translation unit has been
// parsed), kernel_log_sink is a concrete class: its as_sink() body below
// is compiled immediately, in declaration order, and its call to
// log_sink(kernel_log_sink_tag{}, *this) needs log_sink_traits<
// kernel_log_sink_tag>::context_type visible right there (kernel_log_sink_
// tag is not a dependent type, so nothing defers that lookup the way it
// does for syslog_sink). Declaring the traits specialization (with only
// context_type, an incomplete pointer target, and a not-yet-defined log())
// ahead of the class, then defining log() out-of-line afterward, breaks
// that cycle.
class kernel_log_sink;

/** @brief Tag selecting `kernel_log_sink` as a `log_sink` backend. */
struct kernel_log_sink_tag {};

template <> struct log_sink_traits<kernel_log_sink_tag> {
  using context_type = kernel_log_sink;

  static void log(value_ref<context_type> ctx, const log_msg &msg) noexcept;
};

/** @brief Adapter that writes structured records to the FreeBSD kernel's `log(9)`. */
class kernel_log_sink {
public:
  [[nodiscard]] log_sink as_sink() noexcept RELOCO_LIFETIMEBOUND {
    return log_sink(kernel_log_sink_tag{}, *this);
  }

  void log_impl(const log_msg &msg) noexcept {
    if (msg.lvl == level::off) {
      return;
    }

    buffer_sink<256> buffer;
    const auto out = buffer.as_sink();
    if (!msg.logger_name.empty()) {
      microfmt::format_to(out, "[{}] ", msg.logger_name);
    }
    microfmt::format_to(out, "{}", msg.payload);

    // log(9) does its own printf(9)-style formatting; %.*s keeps this a
    // single, allocation-free call regardless of the record's length.
    log(priority_for(msg.lvl), "%.*s\n", static_cast<int>(buffer.view().size()), buffer.view().data());
  }

private:
  static int priority_for(level lvl) noexcept {
    switch (lvl) {
    case level::trace:
    case level::debug:
      return LOG_DEBUG;
    case level::info:
      return LOG_INFO;
    case level::warn:
      return LOG_WARNING;
    case level::err:
      return LOG_ERR;
    case level::critical:
      return LOG_CRIT;
    case level::off:
      return LOG_DEBUG;
    }
    return LOG_DEBUG;
  }
};

inline void log_sink_traits<kernel_log_sink_tag>::log(value_ref<context_type> ctx, const log_msg &msg) noexcept {
  ctx->log_impl(msg);
}

namespace detail {

/** @brief Built-in-shaped replacement: a `logger` backed by `kernel_log_sink`. */
inline logger &built_in_default_logger() noexcept {
  static kernel_log_sink sink_instance;
  static logger instance("kernel", sink_instance.as_sink());
  return instance;
}

} // namespace detail

} // namespace microfmt::log
