// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file dwarf_decoder.hpp @brief DWARF CFI decoder utilizing selective
 * register_context_ref access for virtual register state tracking. */

#include "address_space.hpp"
#include "dwarf_abi.hpp"
#include "dwarf_registers.hpp"
#include "elf_enumerator.hpp"
#include "frame_pointer.hpp"
#include "register_context.hpp"
#include <cstddef>
#include <cstdint>

namespace microfmt {

/**
 * @brief Decoder executing DWARF CFI bytecode against an address space and
 * selective register context.
 * @tparam AbiTraits ABI traits describing registers for the target
 * architecture.
 */
template <typename AbiTraits> class dwarf_cfi_decoder {
public:
  using register_type = typename AbiTraits::register_type;

  static bool read_uleb128(address_space_ref space, uintptr_t &addr,
                           uint64_t &out_val) noexcept {
    uint64_t result = 0;
    unsigned shift = 0;
    uint8_t byte = 0;
    for (int i = 0; i < 10; ++i) {
      if (!space.read_bytes(addr++, &byte, 1))
        return false;
      result |= static_cast<uint64_t>(byte & 0x7F) << shift;
      if (!(byte & 0x80)) {
        out_val = result;
        return true;
      }
      shift += 7;
    }
    return false;
  }

  static bool read_sleb128(address_space_ref space, uintptr_t &addr,
                           int64_t &out_val) noexcept {
    int64_t result = 0;
    unsigned shift = 0;
    uint8_t byte = 0;
    for (int i = 0; i < 10; ++i) {
      if (!space.read_bytes(addr++, &byte, 1))
        return false;
      result |= static_cast<int64_t>(byte & 0x7F) << shift;
      shift += 7;
      if (!(byte & 0x80)) {
        if ((shift < 64) && (byte & 0x40)) {
          result |= (~0ULL << shift);
        }
        out_val = result;
        return true;
      }
    }
    return false;
  }

  // ------------------------------------------------------------------------
  // DWARF CFI Opcodes
  // ------------------------------------------------------------------------
  static constexpr uint8_t DW_CFA_advance_loc = 0x40;
  static constexpr uint8_t DW_CFA_offset = 0x80;
  static constexpr uint8_t DW_CFA_restore = 0xC0;
  static constexpr uint8_t DW_CFA_nop = 0x00;
  static constexpr uint8_t DW_CFA_register = 0x09; // Register-to-register rule
  static constexpr uint8_t DW_CFA_advance_loc1 = 0x02;
  static constexpr uint8_t DW_CFA_advance_loc2 = 0x03;
  static constexpr uint8_t DW_CFA_offset_extended = 0x05;
  static constexpr uint8_t DW_CFA_remember_state = 0x0A;
  static constexpr uint8_t DW_CFA_restore_state = 0x0B;
  static constexpr uint8_t DW_CFA_def_cfa = 0x0C;
  static constexpr uint8_t DW_CFA_def_cfa_register = 0x0D;
  static constexpr uint8_t DW_CFA_def_cfa_offset = 0x0E;
  static constexpr uint8_t DW_CFA_offset_extended_sf = 0x11;
  static constexpr uint8_t DW_CFA_def_cfa_sf = 0x12;
  static constexpr uint8_t DW_CFA_def_cfa_offset_sf = 0x13;

  /**
   * @brief Tracks location type for saved registers (Memory offset vs Live
   * Register).
   */
  enum class reg_rule_type : uint8_t {
    unspecified,
    from_memory_cfa_offset,
    from_register
  };

  struct reg_location {
    reg_rule_type type{reg_rule_type::unspecified};
    int64_t value{0}; // Either offset from CFA or source register number
  };

  struct frame_state {
    uint32_t cfa_reg{AbiTraits::sp_reg};
    int64_t cfa_offset{0};

    reg_location ra_loc{reg_rule_type::from_memory_cfa_offset,
                        -static_cast<int64_t>(AbiTraits::pointer_size)};
    reg_location fp_loc{reg_rule_type::from_memory_cfa_offset,
                        -static_cast<int64_t>(AbiTraits::pointer_size * 2)};

    uintptr_t current_loc{0};
  };

  template <size_t MaxStateStackDepth = 8> struct scratch_context {
    frame_state current_state;
    frame_state state_stack[MaxStateStackDepth];
    size_t stack_depth{0};

    void reset(uintptr_t initial_loc) noexcept {
      current_state = frame_state{};
      current_state.current_loc = initial_loc;
      stack_depth = 0;
    }
  };

  /**
   * @brief Executes FDE bytecode, selectively querying register_context_ref
   * only when virtual registers are needed for CFA resolution or
   * register-to-register mapping.
   */
  template <size_t MaxStateStackDepth>
  static bool
  execute_fde(register_context_ref reg_ctx, uintptr_t fde_addr,
              uintptr_t target_pc, uintptr_t &out_fp, uintptr_t &out_pc,
              scratch_context<MaxStateStackDepth> &scratch) noexcept {
    if (!reg_ctx)
      return false;
    address_space_ref space = reg_ctx.space();

    // Read FDE Header
    uint32_t length = 0;
    if (!space.read_bytes(fde_addr, &length, 4))
      return false;
    if (length == 0 || length == 0xFFFFFFFF)
      return false;

    uintptr_t entry_start = fde_addr + 4;
    uintptr_t entry_end = entry_start + length;

    uint32_t cie_pointer = 0;
    if (!space.read_bytes(entry_start, &cie_pointer, 4))
      return false;
    entry_start += 4;

    uint32_t initial_loc = 0;
    uint32_t address_range = 0;
    if (!space.read_bytes(entry_start, &initial_loc, 4))
      return false;
    entry_start += 4;
    if (!space.read_bytes(entry_start, &address_range, 4))
      return false;
    entry_start += 4;

    if (target_pc < initial_loc || target_pc >= (initial_loc + address_range)) {
      return false;
    }

    scratch.reset(initial_loc);
    auto &state = scratch.current_state;

    // Execute CFI instructions up to target_pc
    uintptr_t inst_ptr = entry_start;
    while (inst_ptr < entry_end && state.current_loc <= target_pc) {
      uint8_t op = 0;
      if (!space.read_bytes(inst_ptr++, &op, 1))
        break;

      if ((op & 0xC0) == DW_CFA_advance_loc) {
        state.current_loc += (op & 0x3F);
      } else if ((op & 0xC0) == DW_CFA_offset) {
        uint8_t reg = op & 0x3F;
        uint64_t offset = 0;
        if (!read_uleb128(space, inst_ptr, offset))
          break;
        int64_t mem_offset =
            -static_cast<int64_t>(offset * AbiTraits::pointer_size);
        if (AbiTraits::is_return_address_register(reg)) {
          state.ra_loc = {reg_rule_type::from_memory_cfa_offset, mem_offset};
        } else if (AbiTraits::is_frame_pointer_register(reg)) {
          state.fp_loc = {reg_rule_type::from_memory_cfa_offset, mem_offset};
        }
      } else {
        switch (op) {
        case DW_CFA_nop:
          break;
        case DW_CFA_register: {
          uint64_t reg = 0;
          uint64_t target_reg = 0;
          if (!read_uleb128(space, inst_ptr, reg) ||
              !read_uleb128(space, inst_ptr, target_reg))
            return false;
          if (AbiTraits::is_return_address_register(
                  static_cast<uint32_t>(reg))) {
            state.ra_loc = {reg_rule_type::from_register,
                            static_cast<int64_t>(target_reg)};
          } else if (AbiTraits::is_frame_pointer_register(
                         static_cast<uint32_t>(reg))) {
            state.fp_loc = {reg_rule_type::from_register,
                            static_cast<int64_t>(target_reg)};
          }
          break;
        }
        case DW_CFA_advance_loc1: {
          uint8_t delta = 0;
          if (!space.read_bytes(inst_ptr++, &delta, 1))
            return false;
          state.current_loc += delta;
          break;
        }
        case DW_CFA_advance_loc2: {
          uint16_t delta = 0;
          if (!space.read_bytes(inst_ptr, &delta, 2))
            return false;
          inst_ptr += 2;
          state.current_loc += delta;
          break;
        }
        case DW_CFA_offset_extended: {
          uint64_t reg = 0;
          uint64_t offset = 0;
          if (!read_uleb128(space, inst_ptr, reg) ||
              !read_uleb128(space, inst_ptr, offset))
            return false;
          int64_t mem_offset =
              -static_cast<int64_t>(offset * AbiTraits::pointer_size);
          if (AbiTraits::is_return_address_register(
                  static_cast<uint32_t>(reg))) {
            state.ra_loc = {reg_rule_type::from_memory_cfa_offset, mem_offset};
          } else if (AbiTraits::is_frame_pointer_register(
                         static_cast<uint32_t>(reg))) {
            state.fp_loc = {reg_rule_type::from_memory_cfa_offset, mem_offset};
          }
          break;
        }
        case DW_CFA_offset_extended_sf: {
          uint64_t reg = 0;
          int64_t offset = 0;
          if (!read_uleb128(space, inst_ptr, reg) ||
              !read_sleb128(space, inst_ptr, offset))
            return false;
          if (AbiTraits::is_return_address_register(
                  static_cast<uint32_t>(reg))) {
            state.ra_loc = {reg_rule_type::from_memory_cfa_offset, offset};
          } else if (AbiTraits::is_frame_pointer_register(
                         static_cast<uint32_t>(reg))) {
            state.fp_loc = {reg_rule_type::from_memory_cfa_offset, offset};
          }
          break;
        }
        case DW_CFA_remember_state:
          if (scratch.stack_depth < MaxStateStackDepth) {
            scratch.state_stack[scratch.stack_depth++] = state;
          }
          break;
        case DW_CFA_restore_state:
          if (scratch.stack_depth > 0) {
            state = scratch.state_stack[--scratch.stack_depth];
          }
          break;
        case DW_CFA_def_cfa: {
          uint64_t reg = 0;
          uint64_t offset = 0;
          if (!read_uleb128(space, inst_ptr, reg) ||
              !read_uleb128(space, inst_ptr, offset))
            return false;
          state.cfa_reg = static_cast<uint32_t>(reg);
          state.cfa_offset = static_cast<int64_t>(offset);
          break;
        }
        case DW_CFA_def_cfa_register: {
          uint64_t reg = 0;
          if (!read_uleb128(space, inst_ptr, reg))
            return false;
          state.cfa_reg = static_cast<uint32_t>(reg);
          break;
        }
        case DW_CFA_def_cfa_offset: {
          uint64_t offset = 0;
          if (!read_uleb128(space, inst_ptr, offset))
            return false;
          state.cfa_offset = static_cast<int64_t>(offset);
          break;
        }
        case DW_CFA_def_cfa_offset_sf: {
          int64_t offset = 0;
          if (!read_sleb128(space, inst_ptr, offset))
            return false;
          state.cfa_offset = offset;
          break;
        }
        default:
          break;
        }
      }
    }

    // 1. SELECTIVE REGISTER ACCESS: Read the CFA base register from the
    // register context
    register_type cfa_base_val = 0;
    if (!reg_ctx.read_raw(state.cfa_reg, &cfa_base_val,
                          AbiTraits::pointer_size)) {
      return false;
    }
    uintptr_t cfa = static_cast<uintptr_t>(cfa_base_val) + state.cfa_offset;

    // 2. Resolve saved Frame Pointer (FP)
    register_type saved_fp = 0;
    if (state.fp_loc.type == reg_rule_type::from_memory_cfa_offset) {
      uintptr_t fp_addr = cfa + state.fp_loc.value;
      space.read_bytes(fp_addr, &saved_fp, AbiTraits::pointer_size);
    } else if (state.fp_loc.type == reg_rule_type::from_register) {
      // SELECTIVE REGISTER ACCESS: Query source register directly via register
      // accessor
      reg_ctx.read_raw(static_cast<uint32_t>(state.fp_loc.value), &saved_fp,
                       AbiTraits::pointer_size);
    }

    // 3. Resolve saved Return Address (RA)
    register_type saved_ra = 0;
    if (state.ra_loc.type == reg_rule_type::from_memory_cfa_offset) {
      uintptr_t ra_addr = cfa + state.ra_loc.value;
      if (!space.read_bytes(ra_addr, &saved_ra, AbiTraits::pointer_size))
        return false;
    } else if (state.ra_loc.type == reg_rule_type::from_register) {
      // SELECTIVE REGISTER ACCESS: Query source register directly via register
      // accessor
      if (!reg_ctx.read_raw(static_cast<uint32_t>(state.ra_loc.value),
                            &saved_ra, AbiTraits::pointer_size))
        return false;
    }

    out_fp = static_cast<uintptr_t>(saved_fp);
    out_pc = AbiTraits::normalize_pc(static_cast<uintptr_t>(saved_ra));

    return true;
  }
};

/**
 * @brief Immutable context describing a DWARF unwinder back to the type-erased
 * handle.
 */
template <typename AbiTraits, size_t MaxStateStackDepth = 8>
struct dwarf_unwinder_context {
  address_space_ref space;
  elf_image_enumerator_ref enumerator;
  elf_image_info *img_storage{nullptr};
  typename dwarf_cfi_decoder<AbiTraits>::template scratch_context<
      MaxStateStackDepth> *dwarf_scratch{nullptr};
};

template <typename AbiTraits, size_t MaxStateStackDepth = 8>
struct dwarf_unwinder_tag {};

template <typename AbiTraits, size_t MaxStateStackDepth = 8>
struct dwarf_unwinder_holder {
  elf_image_info img_storage{};
  typename dwarf_cfi_decoder<AbiTraits>::template scratch_context<
      MaxStateStackDepth>
      dwarf_scratch{};
  dwarf_unwinder_context<AbiTraits, MaxStateStackDepth> ctx;

  dwarf_unwinder_holder(address_space_ref space,
                        elf_image_enumerator_ref enumerator) noexcept
      : ctx{space, enumerator, &img_storage, &dwarf_scratch} {}

  dwarf_unwinder_holder(const dwarf_unwinder_holder &) = delete;
  dwarf_unwinder_holder &operator=(const dwarf_unwinder_holder &) = delete;
  dwarf_unwinder_holder(dwarf_unwinder_holder &&) = delete;
  dwarf_unwinder_holder &operator=(dwarf_unwinder_holder &&) = delete;

  [[nodiscard]] frame_unwinder_ref make_ref() noexcept {
    return frame_unwinder_ref(
        dwarf_unwinder_tag<AbiTraits, MaxStateStackDepth>{}, ctx);
  }
};

} // namespace microfmt

/**
 * @brief Specializes frame_unwinder_traits for DWARF utilizing selective
 * register_context_ref access.
 */
template <typename AbiTraits, size_t MaxStateStackDepth>
struct microfmt::frame_unwinder_traits<
    microfmt::dwarf_unwinder_tag<AbiTraits, MaxStateStackDepth>> {
  using context_type =
      microfmt::dwarf_unwinder_context<AbiTraits, MaxStateStackDepth>;
  using decoder_type = microfmt::dwarf_cfi_decoder<AbiTraits>;

  static bool step(const void *ctx, register_context_ref reg_ctx,
                   uintptr_t &next_fp, uintptr_t &next_pc) noexcept {
    if (!ctx || !reg_ctx)
      return false;
    const auto &cfg = *static_cast<const context_type *>(ctx);

    if (!cfg.enumerator || !cfg.img_storage || !cfg.dwarf_scratch) {
      return false;
    }

    // SELECTIVE REGISTER ACCESS: Read current program counter (PC) from
    // register context
    typename AbiTraits::register_type raw_pc = 0;
    if (!reg_ctx.read_raw(AbiTraits::ra_reg, &raw_pc,
                          AbiTraits::pointer_size)) {
      if (!reg_ctx.read_raw(dwarf::x86_64::PC, &raw_pc,
                            AbiTraits::pointer_size))
        return false;
    }
    uintptr_t fault_pc =
        AbiTraits::normalize_pc(static_cast<uintptr_t>(raw_pc));

    microfmt::elf_image_info &img = *cfg.img_storage;
    img = {};

    if (!cfg.enumerator.find_by_pc(fault_pc, img) || !img.has_debug_frame()) {
      return false;
    }

    uintptr_t curr = img.debug_frame_start;
    while (curr < img.debug_frame_end) {
      uint32_t len = 0;
      if (!cfg.space.read_bytes(curr, &len, 4))
        break;
      if (len == 0)
        break;

      if (decoder_type::execute_fde(reg_ctx, curr, fault_pc, next_fp, next_pc,
                                    *cfg.dwarf_scratch)) {
        return true;
      }

      curr += 4 + len;
    }

    return false;
  }
};