// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file thread.hpp
 * @brief Thread descriptor with telemetry and register context integration. */

#include "../formatters/json.hpp"
#include "../reloco.hpp"
#include "../microfmt.hpp"
#include "metadata_map.hpp"
#include "register_context.hpp"

namespace microfmt {

/**
 * @brief Detailed execution state of an individual thread.
 */
enum class thread_state : uint8_t {
  ready,
  running,
  suspended,
  waiting,
  sleeping,
  terminated
};

} // namespace microfmt

namespace microfmt {
template <> struct formatter<thread_state> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(thread_state state, const sink &out) const noexcept {
    switch (state) {
    case thread_state::ready:
      out.write("READY");
      break;
    case thread_state::running:
      out.write("RUNNING");
      break;
    case thread_state::suspended:
      out.write("SUSPENDED");
      break;
    case thread_state::waiting:
      out.write("WAITING");
      break;
    case thread_state::sleeping:
      out.write("SLEEPING");
      break;
    case thread_state::terminated:
      out.write("TERMINATED");
      break;
    }
  }
};
} // namespace microfmt

namespace microfmt {

/**
 * @brief Thread descriptor containing thread-specific attributes and CPU register context access.
 */
struct RELOCO_POINTER thread_info {
  // Thread-Specific Data
  uint64_t thread_id{0};
  string_view name{};
  uint8_t priority{0};
  thread_state state{thread_state::ready};
  uint32_t cpu_ticks{0};
  uint32_t stack_size{0};
  uint32_t stack_usage{0}; // High-water mark in bytes

  // Zero-Allocation Architecture Register Context View
  register_context_ref regs{};

  // Extended Metadata Callback Hook(Forwards additional custom properties)
  metadata_next_fn_t extended_metadata_fn{nullptr};
  void *extended_metadata_ctx{nullptr};

  /**
   * @brief Stack iteration state supporting phase transition from core to extended properties.
   */
  struct metadata_state {
    const thread_info *thread{nullptr};
    int step{0};
    bool in_extended{false};
  };

  /**
   * @brief Exposes a unified metadata_map view that automatically forwards
   * from core thread properties to any attached extended metadata source.
   */
  [[nodiscard]] constexpr metadata_map
  make_metadata_view(metadata_state &the_state) const & noexcept RELOCO_LIFETIMEBOUND {
    the_state.thread = this;
    the_state.step = 0;
    the_state.in_extended = false;

    return metadata_map(&the_state, [](void *ctx, property_entry &out) noexcept -> bool {
      auto *s = static_cast<metadata_state *>(ctx);
      if (!s || !s->thread)
        return false;

      // Phase 1: Stream core thread properties
      if (!s->in_extended) {
        switch (s->step++) {
        case 0:
          out.key = "thread_id";
          out.val_ptr = &s->thread->thread_id;
          out.print_fn = [](const void *p, const sink &s_out) noexcept {
            detail::format_unsigned<detail::radix::decimal>(s_out, *static_cast<const uint64_t *>(p), false);
          };
          return true;
        case 1:
          out.key = "name";
          out.val_ptr = &s->thread->name;
          out.print_fn = [](const void *p, const sink &s_out) noexcept {
            json::write_escaped_string(s_out, *static_cast<const string_view *>(p));
          };
          return true;
        case 2:
          out.key = "priority";
          out.val_ptr = &s->thread->priority;
          out.print_fn = [](const void *p, const sink &s_out) noexcept {
            detail::format_unsigned<detail::radix::decimal>(s_out, *static_cast<const uint8_t *>(p), false);
          };
          return true;
        case 3:
          out.key = "state";
          out.val_ptr = &s->thread->state;
          out.print_fn = [](const void *p, const sink &s_out) noexcept {
            formatter<thread_state> fmt{};
            format_parse_context parse_context({});
            fmt.parse(parse_context);
            fmt.format(*static_cast<const thread_state *>(p), s_out);
          };
          return true;
        case 4:
          out.key = "cpu_ticks";
          out.val_ptr = &s->thread->cpu_ticks;
          out.print_fn = [](const void *p, const sink &s_out) noexcept {
            detail::format_unsigned<detail::radix::decimal>(s_out, *static_cast<const uint32_t *>(p), false);
          };
          return true;
        case 5:
          out.key = "stack_size_bytes";
          out.val_ptr = &s->thread->stack_size;
          out.print_fn = [](const void *p, const sink &s_out) noexcept {
            detail::format_unsigned<detail::radix::decimal>(s_out, *static_cast<const uint32_t *>(p), false);
          };
          return true;
        case 6:
          out.key = "stack_usage_bytes";
          out.val_ptr = &s->thread->stack_usage;
          out.print_fn = [](const void *p, const sink &s_out) noexcept {
            detail::format_unsigned<detail::radix::decimal>(s_out, *static_cast<const uint32_t *>(p), false);
          };
          return true;
        default:
          s->in_extended = true;
          break; // Transition to phase 2
        }
      }

      // Phase 2: Forward to extended metadata callback if configured
      if (s->in_extended) {
        if (s->thread->extended_metadata_fn && s->thread->extended_metadata_ctx) {
          return s->thread->extended_metadata_fn(s->thread->extended_metadata_ctx, out);
        }
      }

      return false;
    });
  }

  metadata_map make_metadata_view(metadata_state &) const && = delete;
};

} // namespace microfmt