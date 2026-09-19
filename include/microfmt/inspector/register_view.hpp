// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file register_view.hpp
 * @brief Formattable view for rendering CPU register contexts across all
 * architectures. */

#include "../microfmt.hpp"
#include "dwarf_abi.hpp"
#include "dwarf_registers.hpp"
#include "register_context.hpp"
#include <cstddef>
#include <cstdint>

namespace microfmt {

/**
 * @brief Formattable view wrapping a register_context_ref for a specific
 * architecture ABI.
 * @tparam AbiTraits Architecture-specific ABI traits.
 */
template <typename AbiTraits>
class RELOCO_POINTER register_context_view {
public:
  constexpr explicit register_context_view(
      register_context_ref reg_ctx) noexcept
      : reg_ctx_(reg_ctx) {}

  [[nodiscard]] constexpr register_context_ref reg_context() const noexcept {
    return reg_ctx_;
  }

private:
  register_context_ref reg_ctx_;
};

namespace detail {

inline void write_register_separator(const sink &out, size_t printed,
                                     size_t columns) noexcept {
  if (printed == 0)
    return;
  out.write((printed % columns) == 0 ? "\n" : "  ");
}

} // namespace detail

// ============================================================================
// x86_64 (AMD64) Formatter
// ============================================================================
template <> struct formatter<register_context_view<x86_64_abi_traits>> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const register_context_view<x86_64_abi_traits> &view,
              const sink &out) const noexcept {
    register_context_ref reg_ctx = view.reg_context();
    if (!reg_ctx) {
      out.write("<null register context>");
      return;
    }

    using register_traits = x86_64_abi_traits::register_traits;
    uint64_t val = 0;
    size_t printed = 0;

    const auto format_registers = [&](const auto &registers) noexcept {
      for (const auto &reg : registers) {
        if (!reg_ctx.read_raw(reg.index, &val, sizeof(val)))
          continue;
        detail::write_register_separator(out, printed, 2);
        microfmt::format_to(out, MICROFMT_STRING("{}={:#018x}"), reg.name, val);
        ++printed;
      }
    };

    format_registers(register_traits::gpr_registers);
    format_registers(register_traits::system_registers);
  }
};

// ============================================================================
// x86 (IA-32) Formatter
// ============================================================================
template <> struct formatter<register_context_view<x86_abi_traits>> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const register_context_view<x86_abi_traits> &view,
              const sink &out) const noexcept {
    register_context_ref reg_ctx = view.reg_context();
    if (!reg_ctx) {
      out.write("<null register context>");
      return;
    }

    using register_traits = x86_abi_traits::register_traits;
    uint32_t val = 0;
    size_t printed = 0;

    const auto format_registers = [&](const auto &registers) noexcept {
      for (const auto &reg : registers) {
        if (!reg_ctx.read_raw(reg.index, &val, sizeof(val)))
          continue;
        detail::write_register_separator(out, printed, 3);
        microfmt::format_to(out, MICROFMT_STRING("{}={:#010x}"), reg.name, val);
        ++printed;
      }
    };

    format_registers(register_traits::gpr_registers);
    format_registers(register_traits::system_registers);
  }
};

// ============================================================================
// AArch64 (64-bit ARM) Formatter
// ============================================================================
template <> struct formatter<register_context_view<aarch64_abi_traits>> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const register_context_view<aarch64_abi_traits> &view,
              const sink &out) const noexcept {
    register_context_ref reg_ctx = view.reg_context();
    if (!reg_ctx) {
      out.write("<null register context>");
      return;
    }

    using register_traits = aarch64_abi_traits::register_traits;
    uint64_t val = 0;
    size_t printed = 0;

    const auto format_registers = [&](const auto &registers) noexcept {
      for (const auto &reg : registers) {
        if (!reg_ctx.read_raw(reg.index, &val, sizeof(val)))
          continue;
        detail::write_register_separator(out, printed, 2);

        uint64_t display_val = val;
        if (reg.index == dwarf::aarch64::lr ||
            reg.index == dwarf::aarch64::pc) {
          display_val = static_cast<uint64_t>(
              aarch64_abi_traits::normalize_pc(static_cast<uintptr_t>(val)));
        }
        microfmt::format_to(out, MICROFMT_STRING("{}={:#018x}"), reg.name,
                            display_val);
        ++printed;
      }
    };

    format_registers(register_traits::gpr_registers);
    format_registers(register_traits::system_registers);
  }
};

// ============================================================================
// ARM32 (32-bit ARM EABI) Formatter
// ============================================================================
template <> struct formatter<register_context_view<arm_abi_traits>> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const register_context_view<arm_abi_traits> &view,
              const sink &out) const noexcept {
    register_context_ref reg_ctx = view.reg_context();
    if (!reg_ctx) {
      out.write("<null register context>");
      return;
    }

    using register_traits = arm_abi_traits::register_traits;
    uint32_t val = 0;
    size_t printed = 0;

    const auto format_registers = [&](const auto &registers) noexcept {
      for (const auto &reg : registers) {
        if (!reg_ctx.read_raw(reg.index, &val, sizeof(val)))
          continue;
        detail::write_register_separator(out, printed, 3);
        microfmt::format_to(out, MICROFMT_STRING("{}={:#010x}"), reg.name, val);
        ++printed;
      }
    };

    format_registers(register_traits::gpr_registers);
    format_registers(register_traits::system_registers);
  }
};

// ============================================================================
// RISC-V 32-bit Formatter
// ============================================================================
template <> struct formatter<register_context_view<riscv32_abi_traits>> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const register_context_view<riscv32_abi_traits> &view,
              const sink &out) const noexcept {
    register_context_ref reg_ctx = view.reg_context();
    if (!reg_ctx) {
      out.write("<null register context>");
      return;
    }

    using register_traits = riscv32_abi_traits::register_traits;
    uint32_t val = 0;
    size_t printed = 0;

    const auto format_registers = [&](const auto &registers) noexcept {
      for (const auto &reg : registers) {
        if (!reg_ctx.read_raw(reg.index, &val, sizeof(val)))
          continue;
        detail::write_register_separator(out, printed, 3);
        microfmt::format_to(out, MICROFMT_STRING("{}={:#010x}"), reg.name, val);
        ++printed;
      }
    };

    format_registers(register_traits::gpr_registers);
    format_registers(register_traits::system_registers);
  }
};

// ============================================================================
// RISC-V 64-bit Formatter
// ============================================================================
template <> struct formatter<register_context_view<riscv64_abi_traits>> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const register_context_view<riscv64_abi_traits> &view,
              const sink &out) const noexcept {
    register_context_ref reg_ctx = view.reg_context();
    if (!reg_ctx) {
      out.write("<null register context>");
      return;
    }

    using register_traits = riscv64_abi_traits::register_traits;
    uint64_t val = 0;
    size_t printed = 0;

    const auto format_registers = [&](const auto &registers) noexcept {
      for (const auto &reg : registers) {
        if (!reg_ctx.read_raw(reg.index, &val, sizeof(val)))
          continue;
        detail::write_register_separator(out, printed, 2);
        microfmt::format_to(out, MICROFMT_STRING("{}={:#018x}"), reg.name, val);
        ++printed;
      }
    };

    format_registers(register_traits::gpr_registers);
    format_registers(register_traits::system_registers);
  }
};

} // namespace microfmt