// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "address_space.hpp"
#include "dwarf_registers.hpp"
#include "../microfmt.hpp"
#include "../detail/span.hpp"
#include <cstddef>
#include <cstdint>

namespace microfmt {

/**
 * @brief Type-erased vtable for querying architecture-specific registers
 *        by DWARF register index from a debugging execution context frame.
 */
struct register_context_vtable {
  /**
   * @brief Reads a target register value by its DWARF index.
   *
   * @param state Opaque pointer to underlying target thread/frame context state
   * @param space Address space reference for memory-mapped register states if
   * needed
   * @param dwarf_reg_index Architectural DWARF register number (from
   * dwarf_registers.hpp)
   * @param out_val Pointer to destination storage buffer
   * @param val_size Size in bytes of the destination buffer
   * @return true if successfully read, false if unavailable or out of bounds.
   */
  bool (*read_register)(const void *state, address_space_ref space,
                        uint32_t dwarf_reg_index, void *out_val,
                        size_t val_size) noexcept;

  /**
   * @brief Writes a target register value by its DWARF index.
   *
   * @param state Opaque pointer to underlying target thread/frame context state
   * @param space Address space reference
   * @param dwarf_reg_index Architectural DWARF register number
   * @param in_val Pointer to source storage buffer containing the new value
   * @param val_size Size in bytes of the source buffer
   * @return true if successfully written, false if read-only, unavailable, or
   * out of bounds.
   */
  bool (*write_register)(void *state, address_space_ref space,
                         uint32_t dwarf_reg_index, const void *in_val,
                         size_t val_size) noexcept;
};

/**
 * @brief Zero-allocation, type-erased handle for inspecting and modifying CPU
 * registers from a frame context.
 */
class register_context_ref {
public:
  constexpr register_context_ref() noexcept = default;

  template <typename State>
  constexpr register_context_ref(State *state_ptr,
                                 register_context_vtable vtable,
                                 address_space_ref space,
                                 span<std::byte> scratch) noexcept
      : state_(state_ptr), vtable_(vtable), space_(space), scratch_(scratch) {}

  template <typename State>
  constexpr register_context_ref(const State *state_ptr,
                                 register_context_vtable vtable,
                                 address_space_ref space,
                                 span<std::byte> scratch) noexcept
      : state_(const_cast<State *>(state_ptr)), vtable_(vtable), space_(space),
        scratch_(scratch) {}

  [[nodiscard]] constexpr bool is_null() const noexcept {
    return !state_ || (!vtable_.read_register && !vtable_.write_register);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return !is_null();
  }

  /**
   * @brief Reads a register directly into a typed integer or float variable.
   */
  template <typename T>
  [[nodiscard]] bool read(uint32_t dwarf_reg_index,
                          T &out_value) const noexcept {
    if (!vtable_.read_register)
      return false;
    return vtable_.read_register(state_, space_, dwarf_reg_index, &out_value,
                                 sizeof(T));
  }

  /**
   * @brief Reads raw register bytes into a buffer.
   */
  [[nodiscard]] bool read_raw(uint32_t dwarf_reg_index, void *dest,
                              size_t size) const noexcept {
    if (!vtable_.read_register)
      return false;
    return vtable_.read_register(state_, space_, dwarf_reg_index, dest, size);
  }

  /**
   * @brief Writes a typed value into a register by its DWARF index.
   */
  template <typename T>
  [[nodiscard]] bool write(uint32_t dwarf_reg_index,
                           const T &value) const noexcept {
    if (!vtable_.write_register)
      return false;
    return vtable_.write_register(state_, space_, dwarf_reg_index, &value,
                                  sizeof(T));
  }

  /**
   * @brief Writes raw bytes from a source buffer into a register.
   */
  [[nodiscard]] bool write_raw(uint32_t dwarf_reg_index, const void *src,
                               size_t size) const noexcept {
    if (!vtable_.write_register)
      return false;
    return vtable_.write_register(state_, space_, dwarf_reg_index, src, size);
  }

  [[nodiscard]] constexpr address_space_ref space() const noexcept {
    return space_;
  }
  [[nodiscard]] constexpr span<std::byte> scratch() const noexcept {
    return scratch_;
  }

private:
  void *state_{nullptr};
  register_context_vtable vtable_{};
  address_space_ref space_{};
  span<std::byte> scratch_{};
};

} // namespace microfmt