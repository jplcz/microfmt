// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>

/**
 * @file spi.hpp
 * @brief Format simplex and full-duplex SPI transfer views.
 *
 * Use @c spi_duplex, @c spi_write, or @c spi_read to create a transfer view,
 * then format it with either the default diagnostic representation or the
 * @c {:c} compact trace representation.
 */

namespace microfmt {

// ============================================================================
// SPI Modes & Flags
// ============================================================================

enum class spi_mode : uint8_t {
  mode0 = 0, // CPOL=0, CPHA=0
  mode1 = 1, // CPOL=0, CPHA=1
  mode2 = 2, // CPOL=1, CPHA=0
  mode3 = 3  // CPOL=1, CPHA=1
};

enum class spi_status : uint8_t { ok = 0, timeout, crc_err, overrun };

// ============================================================================
// SPI Transfer View Descriptor
// ============================================================================

/** @brief Non-owning simplex or full-duplex SPI transfer descriptor. */
struct MICROFMT_API_CLASS spi_transfer_view {
  span<const uint8_t> mosi{}; // TX data (can be empty for half-duplex RX)
  span<const uint8_t> miso{}; // RX data (can be empty for half-duplex TX)
  uint8_t cs_pin{0};          // Chip select index (CS0, CS1, etc.)
  spi_mode mode{spi_mode::mode0};
  spi_status status{spi_status::ok};
};

// ============================================================================
// Factory Helpers
// ============================================================================

// Full-duplex MOSI/MISO transfer
/** @brief Creates a full-duplex SPI transfer view. */
[[nodiscard]] constexpr spi_transfer_view spi_duplex(span<const uint8_t> mosi, span<const uint8_t> miso, uint8_t cs = 0,
                                                     spi_mode mode = spi_mode::mode0,
                                                     spi_status status = spi_status::ok) noexcept {
  return {mosi, miso, cs, mode, status};
}

template <size_t N1, size_t N2>
[[nodiscard]] constexpr spi_transfer_view spi_duplex(const uint8_t (&mosi)[N1], const uint8_t (&miso)[N2],
                                                     uint8_t cs = 0, spi_mode mode = spi_mode::mode0,
                                                     spi_status status = spi_status::ok) noexcept {
  return {span<const uint8_t>(mosi, N1), span<const uint8_t>(miso, N2), cs, mode, status};
}

// Simplex/Half-duplex MOSI write
/** @brief Creates a write-only SPI transfer view. */
[[nodiscard]] constexpr spi_transfer_view spi_write(span<const uint8_t> mosi, uint8_t cs = 0,
                                                    spi_mode mode = spi_mode::mode0,
                                                    spi_status status = spi_status::ok) noexcept {
  return {mosi, span<const uint8_t>{}, cs, mode, status};
}

template <size_t N>
[[nodiscard]] constexpr spi_transfer_view spi_write(const uint8_t (&mosi)[N], uint8_t cs = 0,
                                                    spi_mode mode = spi_mode::mode0,
                                                    spi_status status = spi_status::ok) noexcept {
  return {span<const uint8_t>(mosi, N), span<const uint8_t>{}, cs, mode, status};
}

// Simplex/Half-duplex MISO read
/** @brief Creates a read-only SPI transfer view. */
[[nodiscard]] constexpr spi_transfer_view spi_read(span<const uint8_t> miso, uint8_t cs = 0,
                                                   spi_mode mode = spi_mode::mode0,
                                                   spi_status status = spi_status::ok) noexcept {
  return {span<const uint8_t>{}, miso, cs, mode, status};
}

template <size_t N>
[[nodiscard]] constexpr spi_transfer_view spi_read(const uint8_t (&miso)[N], uint8_t cs = 0,
                                                   spi_mode mode = spi_mode::mode0,
                                                   spi_status status = spi_status::ok) noexcept {
  return {span<const uint8_t>{}, span<const uint8_t>(miso, N), cs, mode, status};
}

// ============================================================================
// Formatter Specialization for spi_transfer_view
// ============================================================================

template <> struct formatter<spi_transfer_view> {
  bool compact{false};
  bool uppercase_hex{true};

  constexpr void parse(format_parse_context &ctx) noexcept {
    for (char c : ctx.spec()) {
      if (c == 'c' || c == 'C')
        compact = true; // {:c} -> CS0:TX[...]/RX[...]
      if (c == 'x')
        uppercase_hex = false;
    }
  }

  [[nodiscard]] static constexpr microfmt::string_view status_name(spi_status status) noexcept {
    switch (status) {
    case spi_status::ok:
      return "OK";
    case spi_status::timeout:
      return "TIMEOUT";
    case spi_status::crc_err:
      return "CRC_ERR";
    case spi_status::overrun:
      return "OVERRUN";
    }
    return "UNKNOWN";
  }

  void format(const spi_transfer_view &tx, const sink &out) const noexcept {
    const auto &hex = uppercase_hex ? detail::hex_digits_upper : detail::hex_digits_lower;

    auto print_bytes = [&](span<const uint8_t> s) noexcept {
      for (size_t i = 0; i < s.size(); ++i) {
        if (i > 0)
          out.put(' ');
        const uint8_t b = s[i];
        out.put(hex[(b >> 4) & 0x0F]);
        out.put(hex[b & 0x0F]);
      }
    };

    if (compact) {
      // Compact: CS0:TX[9F 00 00]/RX[00 EF 40]
      out.write("CS");
      detail::format_unsigned<detail::radix::decimal>(out, tx.cs_pin, false, 0);
      out.put(':');

      if (!tx.mosi.empty()) {
        out.write("TX[");
        print_bytes(tx.mosi);
        out.put(']');
      }

      if (!tx.mosi.empty() && !tx.miso.empty()) {
        out.put('/');
      }

      if (!tx.miso.empty()) {
        out.write("RX[");
        print_bytes(tx.miso);
        out.put(']');
      }

      if (tx.status != spi_status::ok) {
        out.write("!(");
        out.write(status_name(tx.status));
        out.put(')');
      }
      return;
    }

    // Verbose human-readable transfer inspect:
    // SPI [CS0, Mode 0] (3 B) MOSI: 9F 00 00 | MISO: 00 EF 40 -> OK
    out.write("SPI [CS");
    detail::format_unsigned<detail::radix::decimal>(out, tx.cs_pin, false, 0);
    out.write(", Mode ");
    detail::format_unsigned<detail::radix::decimal>(out, static_cast<uint8_t>(tx.mode), false, 0);
    out.write("] (");

    const size_t total_len = (tx.mosi.size() >= tx.miso.size()) ? tx.mosi.size() : tx.miso.size();
    detail::format_unsigned<detail::radix::decimal>(out, total_len, false, 0);
    out.write(" B)");

    if (!tx.mosi.empty()) {
      out.write(" MOSI: ");
      print_bytes(tx.mosi);
    }

    if (!tx.miso.empty()) {
      if (!tx.mosi.empty())
        out.write(" |");
      out.write(" MISO: ");
      print_bytes(tx.miso);
    }

    out.write(" -> ");
    out.write(status_name(tx.status));
  }
};

} // namespace microfmt
