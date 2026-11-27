// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file itm_sink.hpp
 * @brief Sink for an Arm Cortex-M ITM (Instrumentation Trace Macrocell)
 * stimulus port, whose bytes leave the chip on the SWO (Serial Wire Output)
 * trace pin.
 *
 * Why: ITM lets a debug probe capture text without a UART, and writing a
 * stimulus port is a single store. The port is a memory-mapped register at
 * `0xE0000000 + 4 * port` (ITM base plus four bytes per port); reading it
 * returns bit 0 set when the FIFO (first-in first-out queue) can take data.
 *
 * How: construct with the port address, enable ITM/SWO in your startup code
 * (clock, trace enable, port mask), then format into `as_sink()`. Data is
 * written as 32-, 16- and 8-bit stores. If the FIFO stays full for
 * `max_spins` polls the remaining bytes are dropped and counted, so a missing
 * probe never hangs the program.
 */

#include "../microfmt.hpp"
#include "../reloco.hpp"
#include <cstddef>
#include <cstdint>

namespace microfmt {

/** @brief Writes formatted output to an ITM stimulus port. */
class MICROFMT_API_CLASS itm_sink {
public:
  /** @brief Returns whether the port FIFO can accept a store. */
  using ready_fn_t = bool (*)(std::uintptr_t port) noexcept;
  /** @brief Stores @p value to the port using an access of @p width bytes (1, 2 or 4). */
  using store_fn_t = void (*)(std::uintptr_t port, std::uint32_t value, std::uint8_t width) noexcept;

  /** @brief Constructs a sink over the stimulus register at @p port_addr using real volatile accesses. */
  explicit itm_sink(std::uintptr_t port_addr, std::uint32_t max_spins = 100000) noexcept
      : port_(port_addr), max_spins_(max_spins), ready_fn_(&hw_ready), store_fn_(&hw_store) {}

  /** @brief Constructs a sink with custom register access (used by tests and simulators). */
  itm_sink(std::uintptr_t port_addr, ready_fn_t ready_fn, store_fn_t store_fn,
           std::uint32_t max_spins = 100000) noexcept
      : port_(port_addr), max_spins_(max_spins), ready_fn_(ready_fn), store_fn_(store_fn) {}

  /** @brief Returns the stimulus register address of ITM port @p port (0-31) for the standard ITM base. */
  [[nodiscard]] static constexpr std::uintptr_t port_address(std::uint32_t port) noexcept {
    return static_cast<std::uintptr_t>(0xE0000000U) + 4U * port;
  }

  /** @brief Returns a type-erased `microfmt::sink` view over this instance. */
  [[nodiscard]] sink as_sink() & noexcept RELOCO_LIFETIMEBOUND {
    return sink{this, [](void *ctx, string_view sv) noexcept { static_cast<itm_sink *>(ctx)->write(sv); }};
  }

  /** @brief Sends @p data; drops the unsent remainder if the FIFO never becomes ready. */
  void write(string_view data) noexcept {
    std::size_t pos = 0;
    while (pos < data.size()) {
      const std::size_t left = data.size() - pos;
      const auto width = static_cast<std::uint8_t>(left >= 4 ? 4 : (left >= 2 ? 2 : 1));
      if (!wait_ready()) {
        dropped_ += left;
        return;
      }
      std::uint32_t value = 0;
      for (std::uint8_t i = 0; i < width; ++i) {
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(data[pos + i])) << (8U * i);
      }
      store_fn_(port_, value, width);
      pos += width;
    }
  }

  /** @brief Number of bytes dropped because the FIFO stayed full. */
  [[nodiscard]] std::size_t dropped_bytes() const noexcept { return dropped_; }

private:
  bool wait_ready() const noexcept {
    for (std::uint32_t spin = 0; spin <= max_spins_; ++spin) {
      if (ready_fn_(port_)) {
        return true;
      }
    }
    return false;
  }

  // memory-mapped register access.
  RELOCO_BEGIN_UNSAFE_CODE
  static bool hw_ready(std::uintptr_t port) noexcept {
    return (*reinterpret_cast<volatile std::uint32_t *>(port) & 1U) != 0U;
  }

  static void hw_store(std::uintptr_t port, std::uint32_t value, std::uint8_t width) noexcept {
    switch (width) {
    case 4:
      *reinterpret_cast<volatile std::uint32_t *>(port) = value;
      break;
    case 2:
      *reinterpret_cast<volatile std::uint16_t *>(port) = static_cast<std::uint16_t>(value);
      break;
    default:
      *reinterpret_cast<volatile std::uint8_t *>(port) = static_cast<std::uint8_t>(value);
      break;
    }
  }
  RELOCO_END_UNSAFE_CODE

  std::uintptr_t port_{0};
  std::uint32_t max_spins_{0};
  ready_fn_t ready_fn_{nullptr};
  store_fn_t store_fn_{nullptr};
  std::size_t dropped_{0};
};

} // namespace microfmt
