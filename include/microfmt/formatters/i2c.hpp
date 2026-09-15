// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>

/**
 * @file i2c.hpp
 * @brief Format 7-bit and 10-bit I2C read and write transaction views.
 *
 * Use @c i2c_write, @c i2c_read, or @c i2c_10bit to create a transaction
 * view, then format it with either the default diagnostic representation or
 * the @c {:c} compact trace representation.
 */

namespace microfmt {

// ============================================================================
// I2C Transfer Flags & Status
// ============================================================================

enum class i2c_dir : uint8_t { write = 0, read = 1 };

enum class i2c_status : uint8_t {
  ok = 0,
  nack_addr,
  nack_data,
  arb_lost,
  timeout
};

enum class i2c_flags : uint8_t {
  none = 0,
  ten_bit = 1 << 0, // 10-bit slave address
  nostart = 1 << 1, // Continuation transfer without START condition
  stop = 1 << 2     // Issue STOP condition after this message
};

[[nodiscard]] constexpr i2c_flags operator|(i2c_flags a, i2c_flags b) noexcept {
  return static_cast<i2c_flags>(static_cast<uint8_t>(a) |
                                static_cast<uint8_t>(b));
}

[[nodiscard]] constexpr bool operator&(i2c_flags a, i2c_flags b) noexcept {
  return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

// ============================================================================
// I2C Message View Descriptor
// ============================================================================

/** @brief Non-owning I2C transfer descriptor for formatting. */
struct i2c_msg_view {
  uint16_t addr{0}; // 7-bit (0x00..0x7F) or 10-bit (0x000..0x3FF)
  i2c_dir direction{i2c_dir::write};
  span<const uint8_t> payload{};
  i2c_status status{i2c_status::ok};
  i2c_flags flags{i2c_flags::none};
};

// ============================================================================
// Factory Helpers
// ============================================================================

// Standard 7-bit I2C write transaction
/** @brief Creates a 7-bit I2C write transaction view. */
[[nodiscard]] constexpr i2c_msg_view
i2c_write(uint8_t addr_7bit, span<const uint8_t> data,
          i2c_status status = i2c_status::ok) noexcept {
  return {static_cast<uint16_t>(addr_7bit & 0x7F), i2c_dir::write, data,
          status, i2c_flags::none};
}

template <size_t N>
[[nodiscard]] constexpr i2c_msg_view
i2c_write(uint8_t addr_7bit, const uint8_t (&arr)[N],
          i2c_status status = i2c_status::ok) noexcept {
  return {static_cast<uint16_t>(addr_7bit & 0x7F), i2c_dir::write,
          span<const uint8_t>(arr, N), status, i2c_flags::none};
}

// Standard 7-bit I2C read transaction
/** @brief Creates a 7-bit I2C read transaction view. */
[[nodiscard]] constexpr i2c_msg_view
i2c_read(uint8_t addr_7bit, span<const uint8_t> data,
         i2c_status status = i2c_status::ok) noexcept {
  return {static_cast<uint16_t>(addr_7bit & 0x7F), i2c_dir::read, data,
          status, i2c_flags::none};
}

template <size_t N>
[[nodiscard]] constexpr i2c_msg_view
i2c_read(uint8_t addr_7bit, const uint8_t (&arr)[N],
         i2c_status status = i2c_status::ok) noexcept {
  return {static_cast<uint16_t>(addr_7bit & 0x7F), i2c_dir::read,
          span<const uint8_t>(arr, N), status, i2c_flags::none};
}

// 10-bit Extended address message
/** @brief Creates a 10-bit I2C read or write transaction view. */
[[nodiscard]] constexpr i2c_msg_view
i2c_10bit(uint16_t addr_10bit, i2c_dir dir, span<const uint8_t> data,
          i2c_status status = i2c_status::ok) noexcept {
  return {static_cast<uint16_t>(addr_10bit & 0x3FF), dir, data, status,
          i2c_flags::ten_bit};
}

// ============================================================================
// Formatter Specialization for i2c_msg_view
// ============================================================================

template <> struct formatter<i2c_msg_view> {
  bool compact{false};
  bool uppercase_hex{true};

  constexpr void parse(format_parse_context &ctx) noexcept {
    for (char c : ctx.spec()) {
      if (c == 'c' || c == 'C')
        compact = true; // {:c} -> 0x68:W[0x75]
      if (c == 'x')
        uppercase_hex = false;
    }
  }

  [[nodiscard]] static constexpr std::string_view
  status_name(i2c_status status) noexcept {
    switch (status) {
    case i2c_status::ok:
      return "OK";
    case i2c_status::nack_addr:
      return "NACK_ADDR";
    case i2c_status::nack_data:
      return "NACK_DATA";
    case i2c_status::arb_lost:
      return "ARB_LOST";
    case i2c_status::timeout:
      return "TIMEOUT";
    }
    return "UNKNOWN";
  }

  void format(const i2c_msg_view &msg, const sink &out) const noexcept {
    const char *hex_digits =
        uppercase_hex ? "0123456789ABCDEF" : "0123456789abcdef";
    const bool is_10bit = (msg.flags & i2c_flags::ten_bit);
    const bool is_read = (msg.direction == i2c_dir::read);

    if (compact) {
      // Compact inline trace: 0x68:W[75] or 0x68:R[00 1A]
      out.write("0x");
      detail::format_unsigned(out, msg.addr, 16, uppercase_hex,
                              is_10bit ? 3 : 2);
      out.put(':');
      out.put(is_read ? 'R' : 'W');
      out.put('[');

      for (size_t i = 0; i < msg.payload.size(); ++i) {
        if (i > 0)
          out.put(' ');
        const uint8_t b = msg.payload[i];
        out.put(hex_digits[(b >> 4) & 0x0F]);
        out.put(hex_digits[b & 0x0F]);
      }
      out.put(']');

      if (msg.status != i2c_status::ok) {
        out.write("!(");
        out.write(status_name(msg.status));
        out.put(')');
      }
      return;
    }

    // Verbose bus transaction format:
    // I2C [0x68] WR (1 B) DATA: 75 -> OK
    // I2C [0x68] RD (2 B) DATA: 04 2A -> OK
    out.write("I2C [0x");
    detail::format_unsigned(out, msg.addr, 16, uppercase_hex, is_10bit ? 3 : 2);
    out.put(']');

    if (is_10bit) {
      out.write(" 10b");
    }

    out.write(is_read ? " RD (" : " WR (");
    detail::format_unsigned(out, msg.payload.size(), 10, false, 0);
    out.write(" B)");

    if (!msg.payload.empty()) {
      out.write(" DATA: ");
      for (size_t i = 0; i < msg.payload.size(); ++i) {
        if (i > 0)
          out.put(' ');
        const uint8_t b = msg.payload[i];
        out.put(hex_digits[(b >> 4) & 0x0F]);
        out.put(hex_digits[b & 0x0F]);
      }
    }

    out.write(" -> ");
    out.write(status_name(msg.status));
  }
};

} // namespace microfmt