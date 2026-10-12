// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file extended_msg.hpp
 * @brief Log record extended with execution-context metadata (pid, tid,
 * CPU index, context flags) for "advanced" sinks.
 */

#include "log_msg.hpp"
#include <cstdint>

namespace microfmt::log {

/** @brief CPU index value meaning "unknown". */
inline constexpr std::uint32_t no_cpu = 0xFFFFFFFFU;

/** @brief Well-known bits of @ref log_meta::flags. Bits 16..31 belong to the user. */
enum class log_flag : std::uint32_t {
  none = 0,
  /**
   * The emitting context may block (thread context). When clear (IRQ, NMI,
   * scheduler, ...) a full sink must not wait: it overwrites its oldest
   * data or discards the record.
   */
  can_wait = 1U << 0,
};

/** @brief Execution-context metadata attached to an @ref extended_log_msg. */
struct log_meta {
  /** @brief Bit position of the first user-defined flag in @ref flags. */
  static constexpr unsigned user_flags_shift = 16;

  std::uint32_t pid{0};
  std::uint32_t tid{0};
  std::uint32_t cpu{no_cpu};
  std::uint32_t flags{0};

  [[nodiscard]] constexpr bool has(log_flag f) const noexcept { return (flags & static_cast<std::uint32_t>(f)) != 0; }
  [[nodiscard]] constexpr bool can_wait() const noexcept { return has(log_flag::can_wait); }

  /** @brief Returns the user-defined flag bits, shifted down to bit 0. */
  [[nodiscard]] constexpr std::uint32_t user_flags() const noexcept { return flags >> user_flags_shift; }

  /** @brief Replaces the 16 user-defined flag bits. */
  constexpr void set_user_flags(std::uint32_t user) noexcept {
    flags = (flags & 0xFFFFU) | ((user & 0xFFFFU) << user_flags_shift);
  }

  constexpr void set(log_flag f, bool on = true) noexcept {
    const auto bit = static_cast<std::uint32_t>(f);
    flags = on ? (flags | bit) : (flags & ~bit);
  }
};

/**
 * @brief @ref log_msg plus @ref log_meta. Derives from the original record,
 * so it converts to `const log_msg &` anywhere the plain API is expected.
 */
struct MICROFMT_API_CLASS extended_log_msg : log_msg {
  log_meta meta{};

  constexpr extended_log_msg() = default;
  constexpr extended_log_msg(const log_msg &base, const log_meta &m = {}) : log_msg(base), meta(m) {}
};

} // namespace microfmt::log

RELOCO_TYPE_ID_NAME(microfmt::log::extended_log_msg, "microfmt::log::extended_log_msg");
