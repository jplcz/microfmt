#pragma once

/** @file dwarf_decoder.hpp @brief DWARF CFI (.debug_frame / .eh_frame) decoder
 * and frame unwinder. */

#include "address_space.hpp"
#include "dwarf_abi.hpp"
#include "elf_enumerator.hpp"
#include "frame_pointer.hpp"
#include <cstddef>
#include <cstdint>

namespace microfmt {

template <typename AbiTraits> class dwarf_cfi_decoder {
public:
  using register_type = typename AbiTraits::register_type;

  // Read Unsigned LEB128
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

  // Read Signed LEB128
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

  // Common DWARF CFI Opcodes
  static constexpr uint8_t DW_CFA_advance_loc = 0x40;
  static constexpr uint8_t DW_CFA_offset = 0x80;
  static constexpr uint8_t DW_CFA_restore = 0xC0;
  static constexpr uint8_t DW_CFA_nop = 0x00;
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

  struct frame_state {
    uint32_t cfa_reg{AbiTraits::sp_reg};
    int64_t cfa_offset{0};
    int64_t ra_offset{-static_cast<int64_t>(
        AbiTraits::pointer_size)}; // Default position relative to CFA
    int64_t fp_offset{-static_cast<int64_t>(
        AbiTraits::pointer_size * 2)}; // Default position relative to CFA
    uintptr_t current_loc{0};
  };

  // Caller-supplied scratch container to ensure zero-allocation and minimal
  // stack footprint
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

  // Executes DWARF CFI bytecode to reconstruct registers and stack frame
  template <size_t MaxStateStackDepth>
  static bool
  execute_fde(address_space_ref space, uintptr_t fde_addr, uintptr_t target_pc,
              uintptr_t current_sp, uintptr_t &out_fp, uintptr_t &out_pc,
              scratch_context<MaxStateStackDepth> &scratch) noexcept {
    // Read FDE Header
    uint32_t length = 0;
    if (!space.read_bytes(fde_addr, &length, 4))
      return false;
    if (length == 0 || length == 0xFFFFFFFF)
      return false; // Unsupported 64-bit DWARF

    uintptr_t entry_start = fde_addr + 4;
    uintptr_t entry_end = entry_start + length;

    uint32_t cie_pointer = 0;
    if (!space.read_bytes(entry_start, &cie_pointer, 4))
      return false;
    entry_start += 4;

    // Read initial location and address range covered by FDE
    uint32_t initial_loc = 0;
    uint32_t address_range = 0;
    if (!space.read_bytes(entry_start, &initial_loc, 4))
      return false;
    entry_start += 4;
    if (!space.read_bytes(entry_start, &address_range, 4))
      return false;
    entry_start += 4;

    if (target_pc < initial_loc || target_pc >= (initial_loc + address_range)) {
      return false; // FDE does not cover target PC
    }

    scratch.reset(initial_loc);
    auto &state = scratch.current_state;

    // Execute CFI instructions up to instruction matching target_pc
    uintptr_t inst_ptr = entry_start;
    while (inst_ptr < entry_end && state.current_loc <= target_pc) {
      uint8_t op = 0;
      if (!space.read_bytes(inst_ptr++, &op, 1))
        break;

      if ((op & 0xC0) == DW_CFA_advance_loc) {
        uint8_t delta = op & 0x3F;
        state.current_loc += delta; // Assuming code_align = 1
      } else if ((op & 0xC0) == DW_CFA_offset) {
        uint8_t reg = op & 0x3F;
        uint64_t offset = 0;
        if (!read_uleb128(space, inst_ptr, offset))
          break;
        // Track saved registers
        if (AbiTraits::is_return_address_register(reg)) {
          state.ra_offset =
              -static_cast<int64_t>(offset * AbiTraits::pointer_size);
        } else if (AbiTraits::is_frame_pointer_register(reg)) {
          state.fp_offset =
              -static_cast<int64_t>(offset * AbiTraits::pointer_size);
        }
      } else {
        switch (op) {
        case DW_CFA_nop:
          break;
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
          if (AbiTraits::is_return_address_register(
                  static_cast<uint32_t>(reg))) {
            state.ra_offset =
                -static_cast<int64_t>(offset * AbiTraits::pointer_size);
          } else if (AbiTraits::is_frame_pointer_register(
                         static_cast<uint32_t>(reg))) {
            state.fp_offset =
                -static_cast<int64_t>(offset * AbiTraits::pointer_size);
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
            state.ra_offset = offset;
          } else if (AbiTraits::is_frame_pointer_register(
                         static_cast<uint32_t>(reg))) {
            state.fp_offset = offset;
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
          // Skip or ignore unsupported opcodes instead of failing instantly,
          // allowing partial decoding of complex FDE programs.
          break;
        }
      }
    }

    // Compute Canonical Frame Address (CFA)
    uintptr_t cfa = current_sp + state.cfa_offset;

    // Read saved FP and LR relative to computed CFA
    register_type saved_fp = 0;
    register_type saved_ra = 0;

    uintptr_t fp_addr = cfa + state.fp_offset;
    uintptr_t ra_addr = cfa + state.ra_offset;

    // Safely read pointer_size bytes (4 bytes for RV32/ARM, 8 bytes for
    // RV64/AArch64/x86_64)
    if (!space.read_bytes(fp_addr, &saved_fp, AbiTraits::pointer_size))
      saved_fp = 0;
    if (!space.read_bytes(ra_addr, &saved_ra, AbiTraits::pointer_size))
      return false;

    out_fp = static_cast<uintptr_t>(saved_fp);
    out_pc = static_cast<uintptr_t>(
        saved_ra & ~1U); // Clean thumb/instruction alignment bits if needed

    return true;
  }
};

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

  // Prevent copying or moving to ensure internal pointer stability
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

template <typename AbiTraits, size_t MaxStateStackDepth>
struct microfmt::frame_unwinder_traits<
    microfmt::dwarf_unwinder_tag<AbiTraits, MaxStateStackDepth>> {
  using context_type =
      microfmt::dwarf_unwinder_context<AbiTraits, MaxStateStackDepth>;
  using decoder_type = microfmt::dwarf_cfi_decoder<AbiTraits>;

  static bool step(const void *ctx, uintptr_t current_fp, uintptr_t &next_fp,
                   uintptr_t &next_pc) noexcept {
    if (!ctx || current_fp == 0)
      return false;
    const auto &cfg = *static_cast<const context_type *>(ctx);

    if (!cfg.enumerator || !cfg.img_storage || !cfg.dwarf_scratch) {
      return false;
    }

    uintptr_t sp = current_fp;
    uint32_t return_lr = 0;
    if (!cfg.space.read_bytes(current_fp + 4, &return_lr, 4))
      return false;
    uintptr_t fault_pc = static_cast<uintptr_t>(return_lr & ~1U);

    // Dynamically locate the ELF image owning the faulting program counter
    microfmt::elf_image_info &img = *cfg.img_storage;
    img = {};

    if (!cfg.enumerator.find_by_pc(fault_pc, img) || !img.has_debug_frame()) {
      return false;
    }

    // Scan the module's .debug_frame section for a matching FDE entry
    uintptr_t curr = img.debug_frame_start;
    while (curr < img.debug_frame_end) {
      uint32_t len = 0;
      if (!cfg.space.read_bytes(curr, &len, 4))
        break;
      if (len == 0)
        break;

      // Execute FDE bytecode utilizing the caller-supplied off-stack scratch
      // buffer
      if (decoder_type::execute_fde(cfg.space, curr, fault_pc, sp, next_fp,
                                    next_pc, *cfg.dwarf_scratch)) {
        return true;
      }

      curr += 4 + len;
    }

    return false;
  }
};