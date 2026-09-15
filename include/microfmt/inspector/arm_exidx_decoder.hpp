// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

/** @file arm_exidx_decoder.hpp @brief ARM EXIDX inline unwind bytecode
 * decoder. */

#include "address_space.hpp"
#include <cstdint>

namespace microfmt {

/**
 * @brief CPU register snapshot updated during EXIDX bytecode execution.
 */
struct arm_register_state {
  /// Core registers r0-r15 (r13 = SP, r14 = LR, r15 = PC).
  uint32_t r[16]{0}; // r0-r15 (r13 = SP, r14 = LR, r15 = PC)
  /// VFP double-precision registers d0-d31.
  uint32_t d[32]{0}; // VFP double-precision registers d0-d31
  /// Saved-register tracking flags.
  bool reg_saved[16]{false};
};

/**
 * @brief Decoder for the inline unwind bytecode packed in an EXIDX word.
 */
class arm_exidx_bytecode_decoder {
public:
  /**
   * @brief Executes up to 3 inline unwind bytes from an exidx/extab word.
   * @param space Address space to read from.
   * @param unwind_word Descriptor word (personality + inline bytecode).
   * @param io_sp In/out: virtual stack pointer.
   * @param io_pc In/out: program counter.
   * @param out_regs Receives updated register state.
   * @return `true` when the bytecode was applied; `false` when a personality
   * routine requires an external `.ARM.extab` table read.
   */
  static bool execute_bytecode(address_space_ref space, uint32_t unwind_word,
                               uintptr_t &io_sp, uintptr_t &io_pc,
                               arm_register_state &out_regs) noexcept {
    // Check personality routine bits (bits [31:24])
    uint8_t personality = (unwind_word >> 24) & 0x0F;

    // Personality 0 or 1 with inline data in the lower 3 bytes
    if (personality == 0 || personality == 1) {
      uint8_t byte3 = (unwind_word >> 16) & 0xFF;
      uint8_t byte2 = (unwind_word >> 8) & 0xFF;
      uint8_t byte1 = unwind_word & 0xFF;

      return parse_and_apply_bytes(space, io_sp, io_pc, out_regs, byte3, byte2,
                                   byte1);
    }

    return false; // Complex personality routines require external .ARM.extab
                  // table reads
  }

private:
  /**
   * @brief Applies the three inline opcode bytes in order.
   */
  static bool parse_and_apply_bytes(address_space_ref space, uintptr_t &io_sp,
                                    uintptr_t &io_pc,
                                    arm_register_state &out_regs, uint8_t b1,
                                    uint8_t b2, uint8_t b3) noexcept {
    // Process byte 1
    if (!decode_opcode(space, io_sp, io_pc, out_regs, b1))
      return true; // Finish or complete
    if (b2 == 0)
      return true;
    if (!decode_opcode(space, io_sp, io_pc, out_regs, b2))
      return true;
    if (b3 == 0)
      return true;
    if (!decode_opcode(space, io_sp, io_pc, out_regs, b3))
      return true;

    return true;
  }

  /**
   * @brief Decodes a single inline unwind opcode.
   *
   * Handles `vsp` add/sub adjustments, the FINISH (`0xB0`) opcode, and VFP
   * double-precision register pops.
   *
   * @param space Address space to read from.
   * @param io_sp In/out: virtual stack pointer.
   * @param out_regs Receives updated register state.
   * @param opcode The opcode byte to decode.
   * @return `false` to stop decoding (FINISH), `true` to continue.
   */
  static bool decode_opcode(address_space_ref space, uintptr_t &io_sp,
                            uintptr_t &, arm_register_state &out_regs,
                            uint8_t opcode) noexcept {
    // 00xxxxxx: vsp = vsp + (xxxxxx << 2) + 4
    if ((opcode & 0xC0) == 0x00) {
      uint32_t offset = ((opcode & 0x3F) << 2) + 4;
      io_sp += offset;
      return true;
    }

    // 01xxxxxx: vsp = vsp - (xxxxxx << 2) - 4 (sub sp adjustment)
    if ((opcode & 0xC0) == 0x40) {
      uint32_t offset = ((opcode & 0x3F) << 2) + 4;
      io_sp -= offset;
      return true;
    }

    // 1000iiii iiiiiiii: Pop integer registers (r4-r15)
    if ((opcode & 0xF0) == 0x80) {
      // Byte would be followed by another byte in actual stream, simplified
      // here:
      return true;
    }

    // 10110000: Finish / Refuse to unwind (ret)
    if (opcode == 0xB0) {
      return false; // Stop decoding
    }

    // 10111xxx: Pop VFP double-precision registers d(8) to d(8+xxx) (vpush
    // restoration)
    if ((opcode & 0xF8) == 0xB8) {
      uint8_t count = opcode & 0x07;
      // Pop 'count + 1' double-precision VFP registers from stack
      for (int i = 0; i <= count; ++i, io_sp += 8) {
        uint64_t val = 0;
        if (space.read_bytes(io_sp, &val, 8)) {
          out_regs.d[8 + i] = static_cast<uint32_t>(val & 0xFFFFFFFF);
        }
      }
      return true;
    }

    return true;
  }
};

} // namespace microfmt
