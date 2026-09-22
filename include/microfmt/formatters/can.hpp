// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>

/**
 * @file can.hpp
 * @brief Format standard, extended, remote, and CAN-FD frame views.
 *
 * Use @c can_frame, @c can_extended, or @c can_fd to create a frame view,
 * then format it with either the default diagnostic representation or the
 * @c {:c} candump representation.
 */

namespace microfmt {

// ============================================================================
// CAN Frame Flags
// ============================================================================

enum class can_flags : uint8_t {
  none = 0,
  extended = 1 << 0, // 29-bit Extended ID (vs 11-bit Standard ID)
  rtr = 1 << 1,      // Remote Transmission Request
  fd = 1 << 2,       // CAN-FD Frame (payload > 8 bytes, up to 64 bytes)
  brs = 1 << 3,      // CAN-FD Bit Rate Switch
  esi = 1 << 4       // CAN-FD Error State Indicator
};

[[nodiscard]] constexpr can_flags operator|(can_flags a, can_flags b) noexcept {
  return static_cast<can_flags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

[[nodiscard]] constexpr bool operator&(can_flags a, can_flags b) noexcept {
  return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

// ============================================================================
// CAN Frame View Descriptor
// ============================================================================

/** @brief Non-owning CAN or CAN-FD frame descriptor for formatting. */
struct can_frame_view {
  uint32_t id{0};
  span<const uint8_t> payload{};
  can_flags flags{can_flags::none};
};

// ============================================================================
// Factory Helpers
// ============================================================================

// Standard CAN 2.0 Frame (11-bit ID, max 8 bytes)
/** @brief Creates a standard 11-bit CAN frame view. */
[[nodiscard]] constexpr can_frame_view can_frame(uint16_t id, span<const uint8_t> payload, bool rtr = false) noexcept {
  return {static_cast<uint32_t>(id & 0x7FF), payload, rtr ? can_flags::rtr : can_flags::none};
}

template <size_t N>
[[nodiscard]] constexpr can_frame_view can_frame(uint16_t id, const uint8_t (&arr)[N], bool rtr = false) noexcept {
  return {static_cast<uint32_t>(id & 0x7FF), span<const uint8_t>(arr, N), rtr ? can_flags::rtr : can_flags::none};
}

// Extended CAN Frame (29-bit ID)
/** @brief Creates an extended 29-bit CAN frame view. */
[[nodiscard]] constexpr can_frame_view can_extended(uint32_t id, span<const uint8_t> payload,
                                                    bool rtr = false) noexcept {
  return {id & 0x1FFFFFFF, payload, can_flags::extended | (rtr ? can_flags::rtr : can_flags::none)};
}

// CAN-FD Frame (up to 64 bytes, optional BRS/ESI)
/** @brief Creates a CAN-FD frame view with optional extended, BRS, and ESI flags. */
[[nodiscard]] constexpr can_frame_view can_fd(uint32_t id, span<const uint8_t> payload, bool is_extended = false,
                                              bool brs = false, bool esi = false) noexcept {
  can_flags f = can_flags::fd;
  if (is_extended)
    f = f | can_flags::extended;
  if (brs)
    f = f | can_flags::brs;
  if (esi)
    f = f | can_flags::esi;
  return {is_extended ? (id & 0x1FFFFFFF) : (id & 0x7FF), payload, f};
}

// ============================================================================
// Formatter Specialization for can_frame_view
// ============================================================================

template <> struct formatter<can_frame_view> {
  bool candump_style{false}; // Format like Linux socketcan candump: can0 123#DEADBEEF
  bool uppercase_hex{true};

  constexpr void parse(format_parse_context &ctx) noexcept {
    for (char c : ctx.spec()) {
      if (c == 'c' || c == 'C')
        candump_style = true; // {:c} -> Linux candump log style
      if (c == 'x')
        uppercase_hex = false;
    }
  }

  void format(const can_frame_view &f, const sink &out) const noexcept {

    const auto &hex_digits = uppercase_hex ? detail::hex_digits_upper : detail::hex_digits_lower;
    const bool is_ext = (f.flags & can_flags::extended);
    const bool is_rtr = (f.flags & can_flags::rtr);
    const bool is_fd = (f.flags & can_flags::fd);

    if (candump_style) {
      // Candump compact format: [ID]#[DATA] or [ID]##[FLAGS][DATA] for FD
      detail::format_unsigned<detail::radix::hex>(out, f.id, uppercase_hex, is_ext ? 8 : 3);
      if (is_fd) {
        out.write("##");
        uint8_t fd_flags = 0;
        if (f.flags & can_flags::brs) {
          fd_flags |= 0x1;
        }
        if (f.flags & can_flags::esi) {
          fd_flags |= 0x2;
        }
        RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
        out.put(hex_digits[fd_flags]);
        RELOCO_END_UNSAFE_BUFFER_USAGE;
      } else if (is_rtr) {
        out.put('#');
        out.put('R');
        detail::format_unsigned<detail::radix::decimal>(out, f.payload.size(), false, 0);
        return;
      } else {
        out.put('#');
      }

      for (uint8_t b : f.payload) {
        RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
        out.put(hex_digits[(b >> 4) & 0x0F]);
        out.put(hex_digits[b & 0x0F]);
        RELOCO_END_UNSAFE_BUFFER_USAGE;
      }
      return;
    }

    // Diagnostic human-readable format:
    // CAN [0x18DAF110] EXT DLC=8 DATA: 02 01 0C 00 00 00 00 00
    if (is_fd) {
      out.write("CAN-FD [0x");
    } else {
      out.write("CAN [0x");
    }

    detail::format_unsigned<detail::radix::hex>(out, f.id, uppercase_hex, is_ext ? 8 : 3);
    out.put(']');

    if (is_ext)
      out.write(" EXT");
    if (is_fd) {
      if (f.flags & can_flags::brs)
        out.write(" BRS");
      if (f.flags & can_flags::esi)
        out.write(" ESI");
    }

    if (is_rtr) {
      out.write(" RTR (DLC=");
      detail::format_unsigned<detail::radix::decimal>(out, f.payload.size(), false, 0);
      out.put(')');
      return;
    }

    out.write(" DLC=");
    detail::format_unsigned<detail::radix::decimal>(out, f.payload.size(), false, 0);

    if (!f.payload.empty()) {
      out.write(" DATA: ");
      for (size_t i = 0; i < f.payload.size(); ++i) {
        if (i > 0)
          out.put(' ');
        const uint8_t b = f.payload[i];
        out.put(hex_digits[(b >> 4) & 0x0F]);
        out.put(hex_digits[b & 0x0F]);
      }
    }
  }
};

} // namespace microfmt