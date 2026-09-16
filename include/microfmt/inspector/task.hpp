// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "../formatters/json.hpp"
#include "../markdown.hpp"
#include "address_space.hpp"
#include "elf_enumerator.hpp"
#include "metadata_map.hpp"
#include "thread.hpp"

namespace microfmt {

enum class task_state : uint8_t {
  ready,
  running,
  suspended,
  waiting,
  terminated
};

} // namespace microfmt

namespace microfmt {
template <> struct formatter<task_state> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(task_state state, const sink &out) const noexcept {
    switch (state) {
    case task_state::ready:
      out.write("READY");
      break;
    case task_state::running:
      out.write("RUNNING");
      break;
    case task_state::suspended:
      out.write("SUSPENDED");
      break;
    case task_state::waiting:
      out.write("WAITING");
      break;
    case task_state::terminated:
      out.write("TERMINATED");
      break;
    }
  }
};
} // namespace microfmt

namespace microfmt {

/**
 * @brief Function pointer signature for generating/iterating threads belonging to a task.
 *
 * @param ctx Mutable pointer to caller-owned thread iteration state.
 * @param out Output thread_info structure to populate.
 * @return `true` if a thread was successfully populated, `false` when iteration is complete.
 */
using thread_next_fn_t = bool (*)(void *ctx, thread_info &out) noexcept;

/**
 * @brief Comprehensive system task (Team) descriptor.
 */
struct MICROFMT_POINTER task_info {
  // Classic Task Data
  uint64_t team_id{0}; // PID (Team ID)
  string_view name{};
  task_state state{task_state::ready};
  uint8_t priority{0};
  uint32_t cpu_ticks{0};
  uint32_t memory_usage{0};

  // Memory Context Handle
  address_space_ref space{};

  // ELF Image Enumerator Handle (Modules, EXIDX, and debug frames)
  elf_image_enumerator_ref elf_enumerator{};

  // Task Extended Metadata Callback Hook
  metadata_next_fn_t extended_metadata_fn{nullptr};
  void *extended_metadata_ctx{nullptr};

  // Abstract Thread Generator Hook
  thread_next_fn_t thread_next_fn{nullptr};
  void *thread_next_ctx{nullptr};

  /**
   * @brief Stack iteration state supporting phase transition from classic fields to extended task metadata.
   */
  struct metadata_state {
    const task_info *task{nullptr};
    int step{0};
    bool in_extended{false};
  };

  /**
   * @brief Exposes a unified metadata_map view for classic task data and extended metadata.
   */
  [[nodiscard]] constexpr metadata_map
  make_metadata_view(metadata_state &the_state) const & noexcept MICROFMT_LIFETIMEBOUND {
    the_state.task = this;
    the_state.step = 0;
    the_state.in_extended = false;

    return metadata_map(&the_state, [](void *ctx, property_entry &out) noexcept -> bool {
      auto *s = static_cast<metadata_state *>(ctx);
      if (!s || !s->task)
        return false;

      // Phase 1: Stream classic task properties
      if (!s->in_extended) {
        switch (s->step++) {
        case 0:
          out.key = "team_id";
          out.val_ptr = &s->task->team_id;
          out.print_fn = [](const void *p, const sink &s_out) noexcept {
            detail::format_unsigned(s_out, *static_cast<const uint64_t *>(p), 10, false);
          };
          return true;
        case 1:
          out.key = "name";
          out.val_ptr = &s->task->name;
          out.print_fn = [](const void *p, const sink &s_out) noexcept {
            json::write_escaped_string(s_out, *static_cast<const string_view *>(p));
          };
          return true;
        case 2:
          out.key = "state";
          out.val_ptr = &s->task->state;
          out.print_fn = [](const void *p, const sink &s_out) noexcept {
            formatter<task_state> fmt{};
            format_parse_context parse_context({});
            fmt.parse(parse_context);
            fmt.format(*static_cast<const task_state *>(p), s_out);
          };
          return true;
        case 3:
          out.key = "priority";
          out.val_ptr = &s->task->priority;
          out.print_fn = [](const void *p, const sink &s_out) noexcept {
            detail::format_unsigned(s_out, *static_cast<const uint8_t *>(p), 10, false);
          };
          return true;
        case 4:
          out.key = "cpu_ticks";
          out.val_ptr = &s->task->cpu_ticks;
          out.print_fn = [](const void *p, const sink &s_out) noexcept {
            detail::format_unsigned(s_out, *static_cast<const uint32_t *>(p), 10, false);
          };
          return true;
        case 5:
          out.key = "memory_usage";
          out.val_ptr = &s->task->memory_usage;
          out.print_fn = [](const void *p, const sink &s_out) noexcept {
            detail::format_unsigned(s_out, *static_cast<const uint32_t *>(p), 10, false);
          };
          return true;
        default:
          s->in_extended = true;
          break; // Transition to extended metadata
        }
      }

      // Phase 2: Forward to task extended metadata callback
      if (s->in_extended) {
        if (s->task->extended_metadata_fn && s->task->extended_metadata_ctx) {
          return s->task->extended_metadata_fn(s->task->extended_metadata_ctx, out);
        }
      }

      return false;
    });
  }

  metadata_map make_metadata_view(metadata_state &) const && = delete;

  /**
   * @brief Fetches the next thread belonging to this task using the abstract thread generator.
   */
  [[nodiscard]] constexpr bool get_next_thread(thread_info &out_thread) const noexcept {
    if (thread_next_fn && thread_next_ctx) {
      return thread_next_fn(thread_next_ctx, out_thread);
    }
    return false;
  }
};

} // namespace microfmt