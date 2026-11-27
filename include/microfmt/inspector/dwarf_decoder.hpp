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
#include <tuple>

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

  static bool read_uleb128(address_space_ref space, uintptr_t &addr, uint64_t &out_val) noexcept {
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

  static bool read_sleb128(address_space_ref space, uintptr_t &addr, int64_t &out_val) noexcept {
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
          result |= static_cast<int64_t>(~0ULL << shift);
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
  static constexpr uint8_t DW_CFA_set_loc = 0x01;
  static constexpr uint8_t DW_CFA_advance_loc4 = 0x04;
  static constexpr uint8_t DW_CFA_restore_extended = 0x06;
  static constexpr uint8_t DW_CFA_undefined = 0x07;
  static constexpr uint8_t DW_CFA_same_value = 0x08;
  static constexpr uint8_t DW_CFA_def_cfa_expression = 0x0F;
  static constexpr uint8_t DW_CFA_expression = 0x10;
  static constexpr uint8_t DW_CFA_val_offset = 0x14;
  static constexpr uint8_t DW_CFA_val_offset_sf = 0x15;
  static constexpr uint8_t DW_CFA_val_expression = 0x16;
  static constexpr uint8_t DW_CFA_GNU_args_size = 0x2E;
  static constexpr uint8_t DW_CFA_GNU_negative_offset_extended = 0x2F;

  // ------------------------------------------------------------------------
  // Pointer encodings (DW_EH_PE_*) used by .eh_frame
  // ------------------------------------------------------------------------
  static constexpr uint8_t DW_EH_PE_absptr = 0x00;
  static constexpr uint8_t DW_EH_PE_uleb128 = 0x01;
  static constexpr uint8_t DW_EH_PE_udata2 = 0x02;
  static constexpr uint8_t DW_EH_PE_udata4 = 0x03;
  static constexpr uint8_t DW_EH_PE_udata8 = 0x04;
  static constexpr uint8_t DW_EH_PE_sleb128 = 0x09;
  static constexpr uint8_t DW_EH_PE_sdata2 = 0x0A;
  static constexpr uint8_t DW_EH_PE_sdata4 = 0x0B;
  static constexpr uint8_t DW_EH_PE_sdata8 = 0x0C;
  static constexpr uint8_t DW_EH_PE_pcrel = 0x10;
  static constexpr uint8_t DW_EH_PE_indirect = 0x80;
  static constexpr uint8_t DW_EH_PE_omit = 0xFF;

  /**
   * @brief Tracks location type for saved registers (Memory offset vs Live
   * Register).
   */
  enum class reg_rule_type : uint8_t { unspecified, from_memory_cfa_offset, from_register };

  struct reg_location {
    reg_rule_type type{reg_rule_type::unspecified};
    int64_t value{0}; // Either offset from CFA or source register number
  };

  struct frame_state {
    uint32_t cfa_reg{AbiTraits::sp_reg};
    int64_t cfa_offset{0};

    reg_location ra_loc{reg_rule_type::from_memory_cfa_offset, -static_cast<int64_t>(AbiTraits::pointer_size)};
    reg_location fp_loc{reg_rule_type::from_memory_cfa_offset, -static_cast<int64_t>(AbiTraits::pointer_size * 2)};

    uintptr_t current_loc{0};
  };

  template <size_t MaxStateStackDepth = 8> struct scratch_context {
    frame_state current_state;
    /// Rules the CIE's initial instructions establish. DW_CFA_restore returns to these.
    frame_state initial_state;
    reloco::array<frame_state, MaxStateStackDepth> state_stack;
    size_t stack_depth{0};

    void reset(uintptr_t initial_loc) noexcept {
      current_state = frame_state{};
      current_state.current_loc = initial_loc;
      initial_state = frame_state{};
      stack_depth = 0;
    }
  };

  /// Reads a fixed-width little-endian integer, sign-extended when @p is_signed.
  static bool read_fixed(address_space_ref space, uintptr_t &addr, size_t size, bool is_signed,
                         uint64_t &out) noexcept {
    uint64_t raw = 0;
    if (size == 0 || size > sizeof(raw) || !space.read_bytes(addr, &raw, size))
      return false;
    addr += size;
    if (is_signed && size < sizeof(raw) && (raw & (1ULL << (size * 8 - 1))) != 0)
      raw |= ~0ULL << (size * 8);
    out = raw;
    return true;
  }

  /// Skips one encoded value at @p addr without decoding it.
  static bool skip_encoded(address_space_ref space, uintptr_t &addr, uint8_t enc) noexcept {
    if (enc == DW_EH_PE_omit)
      return true;
    switch (enc & 0x0F) {
    case DW_EH_PE_absptr:
      addr += AbiTraits::pointer_size;
      return true;
    case DW_EH_PE_udata2:
    case DW_EH_PE_sdata2:
      addr += 2;
      return true;
    case DW_EH_PE_udata4:
    case DW_EH_PE_sdata4:
      addr += 4;
      return true;
    case DW_EH_PE_udata8:
    case DW_EH_PE_sdata8:
      addr += 8;
      return true;
    case DW_EH_PE_uleb128: {
      uint64_t value = 0;
      return read_uleb128(space, addr, value);
    }
    case DW_EH_PE_sleb128: {
      int64_t value = 0;
      return read_sleb128(space, addr, value);
    }
    default:
      return false;
    }
  }

  /**
   * @brief Reads one encoded pointer at @p addr. A pc-relative value is resolved
   * against the address of the field itself.
   *
   * Indirect and data-relative encodings are rejected: the decoder cannot
   * follow them without more of the image.
   */
  static bool read_encoded(address_space_ref space, uintptr_t &addr, uint8_t enc, uintptr_t &out) noexcept {
    if (enc == DW_EH_PE_omit || (enc & DW_EH_PE_indirect) != 0)
      return false;
    const uintptr_t field = addr;
    uint64_t value = 0;
    switch (enc & 0x0F) {
    case DW_EH_PE_absptr:
      if (!read_fixed(space, addr, AbiTraits::pointer_size, false, value))
        return false;
      break;
    case DW_EH_PE_udata2:
      if (!read_fixed(space, addr, 2, false, value))
        return false;
      break;
    case DW_EH_PE_udata4:
      if (!read_fixed(space, addr, 4, false, value))
        return false;
      break;
    case DW_EH_PE_udata8:
      if (!read_fixed(space, addr, 8, false, value))
        return false;
      break;
    case DW_EH_PE_sdata2:
      if (!read_fixed(space, addr, 2, true, value))
        return false;
      break;
    case DW_EH_PE_sdata4:
      if (!read_fixed(space, addr, 4, true, value))
        return false;
      break;
    case DW_EH_PE_sdata8:
      if (!read_fixed(space, addr, 8, true, value))
        return false;
      break;
    case DW_EH_PE_uleb128:
      if (!read_uleb128(space, addr, value))
        return false;
      break;
    case DW_EH_PE_sleb128: {
      int64_t signedValue = 0;
      if (!read_sleb128(space, addr, signedValue))
        return false;
      value = static_cast<uint64_t>(signedValue);
      break;
    }
    default:
      return false;
    }
    switch (enc & 0x70) {
    case 0x00:
      break;
    case DW_EH_PE_pcrel:
      value += field;
      break;
    default:
      return false;
    }
    out = static_cast<uintptr_t>(value);
    return true;
  }

  /// What an FDE takes from the CIE it refers to.
  struct cie_info {
    uint64_t code_align{1};
    int64_t data_align{1};
    /// Pointer encoding of the FDE's pc range (`R` augmentation). Absolute when absent.
    uint8_t fde_enc{DW_EH_PE_absptr};
    /// True when the augmentation has `z`, so every FDE carries augmentation data.
    bool has_z{false};
    /// Initial CFI instructions, which every FDE of this CIE starts from.
    uintptr_t insns_begin{0};
    uintptr_t insns_end{0};
  };

  /**
   * @brief Parses the CIE at @p cie_addr. The layout is the same for `.eh_frame`
   * and `.debug_frame`; only the CIE id differs, and the caller checks that.
   */
  static bool parse_cie(address_space_ref space, uintptr_t cie_addr, cie_info &out) noexcept {
    uint32_t length = 0;
    if (!space.read_bytes(cie_addr, &length, 4) || length == 0 || length == 0xFFFFFFFFu)
      return false;
    out = cie_info{};
    const uintptr_t entryEnd = cie_addr + 4 + length;
    uintptr_t p = cie_addr + 8; // past the length and the CIE id
    uint8_t version = 0;
    if (!space.read_bytes(p++, &version, 1))
      return false;
    const uintptr_t augBegin = p;
    uint8_t first = 0;
    if (!space.read_bytes(augBegin, &first, 1))
      return false;
    for (;;) {
      uint8_t c = 0;
      if (!space.read_bytes(p++, &c, 1))
        return false;
      if (c == 0)
        break;
    }
    if (first != 0 && first != 'z')
      return false; // augmentation layout this decoder does not know
    if (version >= 4)
      p += 2; // address_size and segment_size
    if (!read_uleb128(space, p, out.code_align) || !read_sleb128(space, p, out.data_align))
      return false;
    // Return-address column: one byte in CIE version 1, ULEB128 after that.
    if (version == 1) {
      p += 1;
    } else {
      uint64_t raColumn = 0;
      if (!read_uleb128(space, p, raColumn))
        return false;
    }
    if (first == 'z') {
      out.has_z = true;
      uint64_t augLength = 0;
      if (!read_uleb128(space, p, augLength))
        return false;
      const uintptr_t augEnd = p + augLength;
      // The letters name the augmentation data, which follows the length in the same order.
      uintptr_t letter = augBegin + 1;
      for (;;) {
        uint8_t c = 0;
        if (!space.read_bytes(letter++, &c, 1))
          return false;
        if (c == 0)
          break;
        if (c == 'R') {
          if (!space.read_bytes(p++, &out.fde_enc, 1))
            return false;
        } else if (c == 'P') {
          uint8_t enc = 0;
          if (!space.read_bytes(p++, &enc, 1) || !skip_encoded(space, p, enc))
            return false;
        } else if (c == 'L') {
          p += 1;
        } else if (c != 'S' && c != 'B') {
          return false;
        }
      }
      p = augEnd;
    }
    out.insns_begin = p;
    out.insns_end = entryEnd;
    return true;
  }

  /// True when the rules of @p reg are tracked by the decoder (return address or frame pointer).
  static bool is_tracked(uint32_t reg) noexcept {
    return AbiTraits::is_return_address_register(reg) || AbiTraits::is_frame_pointer_register(reg);
  }

  static void set_offset_rule(frame_state &state, uint32_t reg, int64_t offset) noexcept {
    if (AbiTraits::is_return_address_register(reg)) {
      state.ra_loc = {reg_rule_type::from_memory_cfa_offset, offset};
    } else if (AbiTraits::is_frame_pointer_register(reg)) {
      state.fp_loc = {reg_rule_type::from_memory_cfa_offset, offset};
    }
  }

  static void set_register_rule(frame_state &state, uint32_t reg, uint64_t source) noexcept {
    if (AbiTraits::is_return_address_register(reg)) {
      state.ra_loc = {reg_rule_type::from_register, static_cast<int64_t>(source)};
    } else if (AbiTraits::is_frame_pointer_register(reg)) {
      state.fp_loc = {reg_rule_type::from_register, static_cast<int64_t>(source)};
    }
  }

  static void restore_rule(frame_state &state, const frame_state &initial, uint32_t reg) noexcept {
    if (AbiTraits::is_return_address_register(reg)) {
      state.ra_loc = initial.ra_loc;
    } else if (AbiTraits::is_frame_pointer_register(reg)) {
      state.fp_loc = initial.fp_loc;
    }
  }

  /**
   * @brief Runs CFI instructions in [@p begin, @p end).
   *
   * With @p bounded set, stops before the first instruction that starts past
   * @p target_pc. Any opcode the decoder cannot apply fails the run, so the
   * caller falls back to another tier instead of using a wrong rule.
   */
  template <size_t MaxStateStackDepth>
  static bool run_cfi(address_space_ref space, uintptr_t begin, uintptr_t end, const cie_info &cie, bool bounded,
                      uintptr_t target_pc, scratch_context<MaxStateStackDepth> &scratch) noexcept {
    frame_state &state = scratch.current_state;
    uintptr_t ip = begin;
    while (ip < end && (!bounded || state.current_loc <= target_pc)) {
      uint8_t op = 0;
      if (!space.read_bytes(ip++, &op, 1))
        return false;
      const uint8_t high = static_cast<uint8_t>(op & 0xC0);
      const uint32_t low = static_cast<uint32_t>(op & 0x3F);
      if (high == DW_CFA_advance_loc) {
        state.current_loc += static_cast<uintptr_t>(low * cie.code_align);
        continue;
      }
      if (high == DW_CFA_offset) {
        uint64_t factored = 0;
        if (!read_uleb128(space, ip, factored))
          return false;
        set_offset_rule(state, low, static_cast<int64_t>(factored) * cie.data_align);
        continue;
      }
      if (high == DW_CFA_restore) {
        restore_rule(state, scratch.initial_state, low);
        continue;
      }
      switch (op) {
      case DW_CFA_nop:
        break;
      case DW_CFA_set_loc:
        return false;
      case DW_CFA_advance_loc1: {
        uint8_t delta = 0;
        if (!space.read_bytes(ip++, &delta, 1))
          return false;
        state.current_loc += static_cast<uintptr_t>(delta * cie.code_align);
        break;
      }
      case DW_CFA_advance_loc2: {
        uint16_t delta = 0;
        if (!space.read_bytes(ip, &delta, 2))
          return false;
        ip += 2;
        state.current_loc += static_cast<uintptr_t>(delta * cie.code_align);
        break;
      }
      case DW_CFA_advance_loc4: {
        uint32_t delta = 0;
        if (!space.read_bytes(ip, &delta, 4))
          return false;
        ip += 4;
        state.current_loc += static_cast<uintptr_t>(delta * cie.code_align);
        break;
      }
      case DW_CFA_offset_extended: {
        uint64_t reg = 0;
        uint64_t factored = 0;
        if (!read_uleb128(space, ip, reg) || !read_uleb128(space, ip, factored))
          return false;
        set_offset_rule(state, static_cast<uint32_t>(reg), static_cast<int64_t>(factored) * cie.data_align);
        break;
      }
      case DW_CFA_offset_extended_sf: {
        uint64_t reg = 0;
        int64_t factored = 0;
        if (!read_uleb128(space, ip, reg) || !read_sleb128(space, ip, factored))
          return false;
        set_offset_rule(state, static_cast<uint32_t>(reg), factored * cie.data_align);
        break;
      }
      case DW_CFA_restore_extended: {
        uint64_t reg = 0;
        if (!read_uleb128(space, ip, reg))
          return false;
        restore_rule(state, scratch.initial_state, static_cast<uint32_t>(reg));
        break;
      }
      case DW_CFA_undefined:
      case DW_CFA_same_value: {
        uint64_t reg = 0;
        if (!read_uleb128(space, ip, reg))
          return false;
        // Undefined or unchanged rules of the tracked registers are not modelled.
        if (is_tracked(static_cast<uint32_t>(reg)))
          return false;
        break;
      }
      case DW_CFA_register: {
        uint64_t reg = 0;
        uint64_t source = 0;
        if (!read_uleb128(space, ip, reg) || !read_uleb128(space, ip, source))
          return false;
        set_register_rule(state, static_cast<uint32_t>(reg), source);
        break;
      }
      case DW_CFA_remember_state:
        if (scratch.stack_depth < MaxStateStackDepth) {
          scratch.state_stack[scratch.stack_depth++] = state;
        }
        break;
      case DW_CFA_restore_state:
        // Restores the rules only. The location keeps advancing with the program.
        if (scratch.stack_depth > 0) {
          const uintptr_t location = state.current_loc;
          state = scratch.state_stack[--scratch.stack_depth];
          state.current_loc = location;
        }
        break;
      case DW_CFA_def_cfa: {
        uint64_t reg = 0;
        uint64_t offset = 0;
        if (!read_uleb128(space, ip, reg) || !read_uleb128(space, ip, offset))
          return false;
        state.cfa_reg = static_cast<uint32_t>(reg);
        state.cfa_offset = static_cast<int64_t>(offset);
        break;
      }
      case DW_CFA_def_cfa_sf: {
        uint64_t reg = 0;
        int64_t factored = 0;
        if (!read_uleb128(space, ip, reg) || !read_sleb128(space, ip, factored))
          return false;
        state.cfa_reg = static_cast<uint32_t>(reg);
        state.cfa_offset = factored * cie.data_align;
        break;
      }
      case DW_CFA_def_cfa_register: {
        uint64_t reg = 0;
        if (!read_uleb128(space, ip, reg))
          return false;
        state.cfa_reg = static_cast<uint32_t>(reg);
        break;
      }
      case DW_CFA_def_cfa_offset: {
        uint64_t offset = 0;
        if (!read_uleb128(space, ip, offset))
          return false;
        state.cfa_offset = static_cast<int64_t>(offset);
        break;
      }
      case DW_CFA_def_cfa_offset_sf: {
        int64_t factored = 0;
        if (!read_sleb128(space, ip, factored))
          return false;
        state.cfa_offset = factored * cie.data_align;
        break;
      }
      case DW_CFA_expression:
      case DW_CFA_val_expression: {
        uint64_t reg = 0;
        uint64_t length = 0;
        if (!read_uleb128(space, ip, reg) || !read_uleb128(space, ip, length))
          return false;
        if (is_tracked(static_cast<uint32_t>(reg)))
          return false;
        ip += static_cast<uintptr_t>(length);
        break;
      }
      case DW_CFA_val_offset:
      case DW_CFA_val_offset_sf: {
        uint64_t reg = 0;
        uint64_t operand = 0;
        if (!read_uleb128(space, ip, reg) || !read_uleb128(space, ip, operand))
          return false;
        if (is_tracked(static_cast<uint32_t>(reg)))
          return false;
        break;
      }
      case DW_CFA_def_cfa_expression:
        return false;
      case DW_CFA_GNU_args_size: {
        uint64_t size = 0;
        if (!read_uleb128(space, ip, size))
          return false;
        break;
      }
      default:
        return false;
      }
    }
    return true;
  }

  /**
   * @brief Executes the FDE at @p fde_addr for @p target_pc.
   *
   * @param section_start Start of the CFI section. `.debug_frame` CIE pointers are
   * offsets from it.
   * @param eh_frame True for `.eh_frame` layout: CIE id 0, pc-relative pointers
   * described by the CIE's `R` augmentation. False for `.debug_frame`.
   */
  template <size_t MaxStateStackDepth>
  static bool execute_fde(register_context_ref reg_ctx, uintptr_t fde_addr, uintptr_t section_start, bool eh_frame,
                          uintptr_t target_pc, uintptr_t &out_fp, uintptr_t &out_pc,
                          scratch_context<MaxStateStackDepth> &scratch) noexcept {
    if (!reg_ctx)
      return false;
    address_space_ref space = reg_ctx.space();

    uint32_t length = 0;
    if (!space.read_bytes(fde_addr, &length, 4) || length == 0 || length == 0xFFFFFFFFu)
      return false;
    const uintptr_t idField = fde_addr + 4;
    const uintptr_t entryEnd = idField + length;
    uint32_t cieId = 0;
    if (!space.read_bytes(idField, &cieId, 4))
      return false;

    // .eh_frame: the id is the distance from its own field back to the CIE.
    // .debug_frame: the id is the CIE's offset from the start of the section.
    const uintptr_t cieAddr = eh_frame ? idField - static_cast<uintptr_t>(cieId) : section_start + cieId;
    cie_info cie;
    if (!parse_cie(space, cieAddr, cie))
      return false;

    uintptr_t p = idField + 4;
    uintptr_t initialLoc = 0;
    uintptr_t addressRange = 0;
    if (eh_frame) {
      if (!read_encoded(space, p, cie.fde_enc, initialLoc))
        return false;
      // The range has the same width as the pc field, but is never pc-relative.
      if (!read_encoded(space, p, static_cast<uint8_t>(cie.fde_enc & 0x0F), addressRange))
        return false;
    } else {
      uint64_t lo = 0;
      uint64_t range = 0;
      if (!read_fixed(space, p, AbiTraits::pointer_size, false, lo) ||
          !read_fixed(space, p, AbiTraits::pointer_size, false, range))
        return false;
      initialLoc = static_cast<uintptr_t>(lo);
      addressRange = static_cast<uintptr_t>(range);
    }
    if (cie.has_z) {
      uint64_t augLength = 0;
      if (!read_uleb128(space, p, augLength))
        return false;
      p += static_cast<uintptr_t>(augLength);
    }
    if (target_pc < initialLoc || target_pc - initialLoc >= addressRange)
      return false;

    scratch.reset(initialLoc);
    if (!run_cfi(space, cie.insns_begin, cie.insns_end, cie, false, 0, scratch))
      return false;
    scratch.initial_state = scratch.current_state;
    if (!run_cfi(space, p, entryEnd, cie, true, target_pc, scratch))
      return false;
    const frame_state &state = scratch.current_state;

    // 1. SELECTIVE REGISTER ACCESS: Read the CFA base register from the
    // register context
    register_type cfa_base_val = 0;
    if (!reg_ctx.read_raw(state.cfa_reg, &cfa_base_val, AbiTraits::pointer_size)) {
      return false;
    }
    uintptr_t cfa = static_cast<uintptr_t>(cfa_base_val) + static_cast<uintptr_t>(state.cfa_offset);

    // 2. Resolve saved Frame Pointer (FP)
    register_type saved_fp = 0;
    if (state.fp_loc.type == reg_rule_type::from_memory_cfa_offset) {
      uintptr_t fp_addr = cfa + static_cast<uintptr_t>(state.fp_loc.value);
      std::ignore = space.read_bytes(fp_addr, &saved_fp, AbiTraits::pointer_size);
    } else if (state.fp_loc.type == reg_rule_type::from_register) {
      // SELECTIVE REGISTER ACCESS: Query source register directly via register
      // accessor
      std::ignore = reg_ctx.read_raw(static_cast<uint32_t>(state.fp_loc.value), &saved_fp, AbiTraits::pointer_size);
    }

    // 3. Resolve saved Return Address (RA)
    register_type saved_ra = 0;
    if (state.ra_loc.type == reg_rule_type::from_memory_cfa_offset) {
      uintptr_t ra_addr = cfa + static_cast<uintptr_t>(state.ra_loc.value);
      if (!space.read_bytes(ra_addr, &saved_ra, AbiTraits::pointer_size))
        return false;
    } else if (state.ra_loc.type == reg_rule_type::from_register) {
      // SELECTIVE REGISTER ACCESS: Query source register directly via register
      // accessor
      if (!reg_ctx.read_raw(static_cast<uint32_t>(state.ra_loc.value), &saved_ra, AbiTraits::pointer_size))
        return false;
    }

    out_fp = static_cast<uintptr_t>(saved_fp);
    out_pc = AbiTraits::normalize_pc(static_cast<uintptr_t>(saved_ra));

    // reg_ctx is always a mutable scratch snapshot (a dead-process register
    // view or a local ucontext_t/GPR-array copy), never a live process to
    // resume -- write the caller's recovered SP (CFA)/FP/RA back into it so a
    // *subsequent* step (whether another DWARF FDE for the caller, or a
    // different unwinder tier) sees the caller's live registers instead of
    // silently re-reading this frame's now-stale values (which would
    // otherwise loop forever re-resolving the same frame). Best-effort:
    // read-only register contexts still let this single step succeed via
    // out_fp/out_pc, they just can't chain further through this tier.
    auto cfa_reg_val = static_cast<register_type>(cfa);
    std::ignore = reg_ctx.write_raw(AbiTraits::sp_reg, &cfa_reg_val, AbiTraits::pointer_size);
    std::ignore = reg_ctx.write_raw(AbiTraits::fp_reg, &saved_fp, AbiTraits::pointer_size);
    std::ignore = reg_ctx.write_raw(AbiTraits::ra_reg, &saved_ra, AbiTraits::pointer_size);

    return true;
  }
};

/**
 * @brief Immutable context describing a DWARF unwinder back to the type-erased
 * handle.
 */
template <typename AbiTraits, size_t MaxStateStackDepth = 8> struct dwarf_unwinder_context {
  address_space_ref space;
  elf_image_enumerator_ref enumerator;
  elf_image_info *img_storage{nullptr};
  typename dwarf_cfi_decoder<AbiTraits>::template scratch_context<MaxStateStackDepth> *dwarf_scratch{nullptr};
};

template <typename AbiTraits, size_t MaxStateStackDepth = 8> struct dwarf_unwinder_tag {};

template <typename AbiTraits, size_t MaxStateStackDepth = 8> struct dwarf_unwinder_holder {
  elf_image_info img_storage{};
  typename dwarf_cfi_decoder<AbiTraits>::template scratch_context<MaxStateStackDepth> dwarf_scratch{};
  dwarf_unwinder_context<AbiTraits, MaxStateStackDepth> ctx;

  dwarf_unwinder_holder(address_space_ref space, elf_image_enumerator_ref enumerator) noexcept
      : ctx{space, enumerator, &img_storage, &dwarf_scratch} {}

  dwarf_unwinder_holder(const dwarf_unwinder_holder &) = delete;
  dwarf_unwinder_holder &operator=(const dwarf_unwinder_holder &) = delete;
  dwarf_unwinder_holder(dwarf_unwinder_holder &&) = delete;
  dwarf_unwinder_holder &operator=(dwarf_unwinder_holder &&) = delete;

  [[nodiscard]] frame_unwinder_ref make_ref() & noexcept {
    return frame_unwinder_ref(dwarf_unwinder_tag<AbiTraits, MaxStateStackDepth>{}, ctx);
  }
};

} // namespace microfmt

/**
 * @brief Specializes frame_unwinder_traits for DWARF utilizing selective
 * register_context_ref access.
 */
template <typename AbiTraits, size_t MaxStateStackDepth>
struct microfmt::frame_unwinder_traits<microfmt::dwarf_unwinder_tag<AbiTraits, MaxStateStackDepth>> {
  using context_type = microfmt::dwarf_unwinder_context<AbiTraits, MaxStateStackDepth>;
  using decoder_type = microfmt::dwarf_cfi_decoder<AbiTraits>;

  /**
   * @brief Unwinds one frame using the module's DWARF CFI FDE and
   * register_context_ref.
   *
   * @param context The @ref dwarf_unwinder_context.
   * @param reg_ctx Target register context handle.
   * @param current_pc Program counter of the frame being unwound *from*,
   * supplied by the caller/iterator; used only to locate the FDE that covers
   * this frame (previously this was read out of @c AbiTraits::ra_reg, which
   * required callers to pre-seed that register with the crash PC before the
   * first step -- no longer necessary).
   * @param next_fp Receives the caller's frame pointer.
   * @param next_pc Receives the caller's program counter.
   * @return `true` on success.
   */
  static bool step(value_ref<const context_type> context, register_context_ref reg_ctx, uintptr_t current_pc,
                   uintptr_t &next_fp, uintptr_t &next_pc) noexcept {
    if (!reg_ctx)
      return false;

    if (!context->enumerator || !context->img_storage || !context->dwarf_scratch) {
      return false;
    }

    uintptr_t fault_pc = AbiTraits::normalize_pc(current_pc);

    microfmt::elf_image_info &img = *context->img_storage;
    img = {};

    if (!context->enumerator.find_by_pc(fault_pc, img) || !img.has_debug_frame()) {
      return false;
    }

    // The first entry is a CIE: id 0 in .eh_frame, 0xffffffff in .debug_frame.
    uint32_t firstId = 0;
    if (!context->space.read_bytes(img.debug_frame_start + 4, &firstId, 4))
      return false;
    const bool ehFrame = firstId == 0;

    uintptr_t curr = img.debug_frame_start;
    while (curr < img.debug_frame_end) {
      uint32_t len = 0;
      if (!context->space.read_bytes(curr, &len, 4))
        break;
      if (len == 0 || len == 0xFFFFFFFFu)
        break; // terminator, or a 64-bit entry this decoder does not read

      uint32_t id = 0;
      if (!context->space.read_bytes(curr + 4, &id, 4))
        break;
      const bool isCie = ehFrame ? id == 0 : id == 0xFFFFFFFFu;
      if (!isCie && decoder_type::execute_fde(reg_ctx, curr, img.debug_frame_start, ehFrame, fault_pc, next_fp, next_pc,
                                              *context->dwarf_scratch)) {
        return true;
      }

      curr += 4 + len;
    }

    return false;
  }
};