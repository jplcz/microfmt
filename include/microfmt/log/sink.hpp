// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file sink.hpp @brief Type-erased structured logging sinks and console output. */

#include "../formatters/ansi.hpp"
#include "../formatters/chrono.hpp"
#include "../sinks/stdio.hpp"
#include "log_msg.hpp"
#include <cstddef>
#include <string_view>
#include <type_traits>

namespace microfmt::log {

// ============================================================================
// Traits-Based Log Sink Provider Pattern
// ============================================================================
//
// Mirrors reloco's context_type-based provider pattern (see reloco's
// `docs/extending.md`, and `microfmt::address_space_ref`/
// `microfmt::sink_provider_ref` for the same shape elsewhere in this
// library): an empty tag selects a `log_sink_traits<Tag>` specialization
// declaring a `context_type` and a mandatory `log` operation (plus an
// optional `flush`); `log_sink` is the type-erased, two-word handle built
// from a tag and (for stateful backends) a context reference. No virtual
// interfaces, no allocation, and no runtime registration are involved.

/**
 * @brief Static customization point describing a log sink backend.
 *
 * Specialize for a tag type to provide `context_type` and `log`. Add
 * `flush` to expose that optional operation; omitting it makes it report as
 * unsupported through @ref log_sink::can_flush instead of failing at
 * compile time.
 *
 * For a stateless backend (writes purely through global/static state, e.g.
 * a free function taking no context), declare `using context_type = void;`
 * and drop the `value_ref<context_type>` parameter from every operation.
 *
 * @tparam Tag Tag identifying the log sink implementation.
 */
template <typename Tag> struct log_sink_traits;

namespace detail {

template <typename Tag, typename = void> struct has_log_sink_flush : std::false_type {};

template <typename Tag>
struct has_log_sink_flush<Tag, std::void_t<decltype(log_sink_traits<Tag>::flush(
                                    std::declval<value_ref<typename log_sink_traits<Tag>::context_type>>()))>>
    : std::true_type {};

template <typename Tag, typename = void> struct has_stateless_log_sink_flush : std::false_type {};

template <typename Tag>
struct has_stateless_log_sink_flush<Tag, std::void_t<decltype(log_sink_traits<Tag>::flush())>> : std::true_type {};

} // namespace detail

/**
 * @brief Type-erased, two-word handle to a log sink backend, plus the
 * severity threshold below which records are dropped before ever reaching
 * the backend.
 *
 * Packs a context pointer and a vtable pointer into two words: no virtual
 * base class, no RTTI, and no allocation of its own. The bound context (for
 * stateful tags) must outlive every `log_sink` built from it.
 */
class RELOCO_POINTER log_sink {
public:
  /**
   * @brief Virtual table of log sink operations.
   */
  struct vtable {
    /** @brief Emits a record. See @ref log_sink::log. */
    void (*log)(void *ctx, const log_msg &msg) noexcept;
    /**
     * @brief Flushes buffered output. `nullptr` for backends whose traits
     * omit `flush`. See @ref log_sink::flush.
     */
    void (*flush)(void *ctx) noexcept;
  };

  /**
   * @brief Constructs an empty (invalid) handle.
   */
  constexpr log_sink() noexcept = default;

  /**
   * @brief Constructs a handle for a stateless log sink tag.
   * @tparam Tag Log sink tag type.
   * @tparam Traits Specialized traits, enabled when `context_type` is `void`.
   * @param lvl Severity threshold below which records are dropped.
   */
  template <typename Tag, typename Traits = log_sink_traits<Tag>,
            std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  constexpr explicit log_sink(Tag, level lvl = level::trace) noexcept : vtbl_(&s_vtbl<Tag>), lvl_(lvl) {}

  /**
   * @brief Constructs a handle for a stateful log sink tag.
   * @tparam Tag Log sink tag type.
   * @tparam Context Concrete context type.
   * @tparam Traits Specialized traits, enabled when `context_type` is
   * non-void and @p Context converts to it.
   * @param ctx Context object backing the writes.
   * @param lvl Severity threshold below which records are dropped.
   */
  template <typename Tag, typename Context, typename Traits = log_sink_traits<Tag>,
            std::enable_if_t<!std::is_void_v<typename Traits::context_type> &&
                                 std::is_convertible_v<Context *, typename Traits::context_type *>,
                             int> = 0>
  constexpr log_sink(Tag, Context &ctx RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS,
                      level lvl = level::trace) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag>), lvl_(lvl) {}

  // Refuses to bind to a temporary context, which would leave ctx_ dangling
  // the moment this constructor returns.
  template <typename Tag, typename Context, std::enable_if_t<!std::is_lvalue_reference_v<Context>, int> = 0>
  constexpr log_sink(Tag, Context &&, level = level::trace) = delete;

  /**
   * @brief Emits @p msg to the underlying backend, honoring both the
   * record's own level check and this sink's configured threshold.
   */
  void log(const log_msg &msg) noexcept {
    if (vtbl_ && msg.lvl >= lvl_) {
      vtbl_->log(ctx_.get(), msg);
    }
  }

  /**
   * @brief Reports whether the bound backend supports `flush`.
   */
  [[nodiscard]] constexpr bool can_flush() const noexcept { return vtbl_ && vtbl_->flush; }

  /**
   * @brief Flushes buffered output. A no-op when @ref can_flush is `false`.
   */
  void flush() noexcept {
    if (can_flush()) {
      vtbl_->flush(ctx_.get());
    }
  }

  void set_level(level l) noexcept { lvl_ = l; }
  [[nodiscard]] constexpr bool should_log(level l) const noexcept { return l >= lvl_; }

  /**
   * @brief Reports whether the handle is bound to a log sink backend.
   */
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return vtbl_ != nullptr; }

private:
  template <typename Tag> static void log_entry(void *ctx, const log_msg &msg) noexcept {
    using context_type = typename log_sink_traits<Tag>::context_type;
    if constexpr (std::is_void_v<context_type>) {
      (void)ctx;
      log_sink_traits<Tag>::log(msg);
    } else {
      auto &typed = *static_cast<context_type *>(ctx);
      log_sink_traits<Tag>::log(value_ref<context_type>(typed), msg);
    }
  }

  template <typename Tag> [[nodiscard]] static constexpr auto flush_entry() noexcept {
    using context_type = typename log_sink_traits<Tag>::context_type;
    if constexpr (std::is_void_v<context_type>) {
      if constexpr (detail::has_stateless_log_sink_flush<Tag>::value) {
        return +[](void *) noexcept { log_sink_traits<Tag>::flush(); };
      } else {
        return static_cast<void (*)(void *) noexcept>(nullptr);
      }
    } else if constexpr (detail::has_log_sink_flush<Tag>::value) {
      return +[](void *ctx) noexcept {
        auto &typed = *static_cast<context_type *>(ctx);
        log_sink_traits<Tag>::flush(value_ref<context_type>(typed));
      };
    } else {
      return static_cast<void (*)(void *) noexcept>(nullptr);
    }
  }

  template <typename Tag> static constexpr vtable s_vtbl{&log_entry<Tag>, flush_entry<Tag>()};

  value_ptr<void> ctx_{};
  const vtable *vtbl_{nullptr};
  level lvl_{level::trace};
};

// ============================================================================
// Built-in Standard Sinks
// ============================================================================

// ANSI Color Console Sink (stdout / stderr)

/** @brief Tag selecting `stdout_color_sink<LineBufCap>` as a `log_sink` backend. */
template <std::size_t LineBufCap> struct stdout_color_sink_tag {};

/** @brief ANSI-colorized stdout adapter for structured log records. */
template <size_t LineBufCap = 256> class stdout_color_sink {
public:
  stdout_color_sink() noexcept = default;

  [[nodiscard]] log_sink as_sink() noexcept RELOCO_LIFETIMEBOUND {
    return log_sink(stdout_color_sink_tag<LineBufCap>{}, *this);
  }

  void log_impl(const log_msg &msg) noexcept {
    buffer_sink<LineBufCap> buf;
    auto out = buf.as_sink();

    // Timestamp: [HH:MM:SS.mmm]
    microfmt::format_to(out, "[{:t}] ", msg.time);

    // Logger Tag
    if (!msg.logger_name.empty()) {
      microfmt::format_to(out, "[{}] ", msg.logger_name);
    }

    // Colorized Level Tag
    switch (msg.lvl) {
    case level::trace:
      microfmt::format_to(out, "[{}] ", ansi::gray("TRACE"));
      break;
    case level::debug:
      microfmt::format_to(out, "[{}] ", ansi::cyan("DEBUG"));
      break;
    case level::info:
      microfmt::format_to(out, "[{}] ", ansi::green("INFO "));
      break;
    case level::warn:
      microfmt::format_to(out, "[{}] ", ansi::yellow("WARN "));
      break;
    case level::err:
      microfmt::format_to(out, "[{}] ", ansi::red("ERROR"));
      break;
    case level::critical:
      microfmt::format_to(out, "[{}] ", ansi::styled(
                                "CRIT ",
                                ansi::style{ansi::color::bright_white,
                                            ansi::color::red,
                                            ansi::attribute::bold}));
      break;
    default:
      break;
    }

    // Message payload + newline
    microfmt::format_to(out, "{}\n", msg.payload);

    // Output to direct stdout file descriptor
    auto term = stdout_sink();
    term.write(buf.view());
  }
};

template <std::size_t LineBufCap> struct log_sink_traits<stdout_color_sink_tag<LineBufCap>> {
  using context_type = stdout_color_sink<LineBufCap>;

  static void log(value_ref<context_type> ctx, const log_msg &msg) noexcept { ctx->log_impl(msg); }
};

} // namespace microfmt::log