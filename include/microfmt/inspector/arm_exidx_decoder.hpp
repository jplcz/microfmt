// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file arm_exidx_decoder.hpp @brief ARM EXIDX inline unwind bytecode
 * decoder. */

#include "address_space.hpp"
#include "register_context.hpp"
#include <cstdint>

namespace microfmt {

/**
 * @brief Decoder for the inline unwind bytecode packed in an EXIDX word,
 *        writing restored register states directly via register_context_ref.
 */
class MICROFMT_API_CLASS arm_exidx_bytecode_decoder {
public:
  /**
   * @brief Executes up to 3 inline unwind bytes from an exidx/extab word.
   * @param space Address space to read stack values from.
   * @param unwind_word Descriptor word (personality + inline bytecode).
   * @param io_sp In/out: virtual stack pointer.
   * @param io_pc In/out: program counter.
   * @param reg_ctx Target register context handle to write recovered registers
   * into.
   * @return `true` when the bytecode was applied; `false` when a personality
   * routine requires an external `.ARM.extab` table read.
   */
  RELOCO_UNSAFE_BUFFER_USAGE
  static bool execute_bytecode(address_space_ref space, uint32_t unwind_word, uintptr_t &io_sp, uintptr_t &,
                               register_context_ref reg_ctx) noexcept {
    // Check personality routine bits (bits [31:24])
    uint8_t personality = (unwind_word >> 24) & 0x0F;

    // Personality 0 or 1 with inline data in the lower 3 bytes
    if (personality == 0 || personality == 1) {
      uint8_t bytes[3] = {
          static_cast<uint8_t>((unwind_word >> 16) & 0xFF), // Byte 1
          static_cast<uint8_t>((unwind_word >> 8) & 0xFF),  // Byte 2
          static_cast<uint8_t>(unwind_word & 0xFF)          // Byte 3
      };

      return parse_and_apply_bytes(space, io_sp, reg_ctx, bytes);
    }

    return false; // Complex personality routines require external .ARM.extab
                  // reads
  }

private:
  /**
   * @brief Decodes a single inline unwind opcode.
   *
   * Handles `vsp` add/sub adjustments, the FINISH (`0xB0`) opcode, and VFP
   * double-precision register pops.
   *
   * @param space Address space to read from.
   * @param io_sp In/out: virtual stack pointer.
   * @param reg_ctx Receives updated register state.
   * @param bytes The opcode byte to decode.
   * @return `false` to stop decoding (FINISH), `true` to continue.
   */
  RELOCO_UNSAFE_BUFFER_USAGE
  static bool parse_and_apply_bytes(address_space_ref space, uintptr_t &io_sp, register_context_ref reg_ctx,
                                    const uint8_t bytes[3]) noexcept {
    int idx = 0;
    while (idx < 3) {
      uint8_t opcode = bytes[idx++];
      if (opcode == 0x00) {
        continue; // NOP / Padding byte
      }

      // 0xB0 = FINISH opcode (terminate inline bytecode block)
      if (opcode == 0xB0) {
        return true;
      }

      // 00xxxxxx: vsp = vsp + (xxxxxx << 2) + 4
      if ((opcode & 0xC0) == 0x00) {
        uint32_t offset = (static_cast<uint32_t>(opcode & 0x3F) << 2U) + 4U;
        io_sp += offset;
      }
      // 01xxxxxx: vsp = vsp - (xxxxxx << 2) - 4
      else if ((opcode & 0xC0) == 0x40) {
        uint32_t offset = (static_cast<uint32_t>(opcode & 0x3F) << 2U) + 4U;
        io_sp -= offset;
      }
      // 1000iiii iiiiiiii: Pop integer registers (r4-r15)
      else if ((opcode & 0xF0) == 0x80) {
        if (idx >= 3)
          break;
        uint8_t low_mask = bytes[idx++];
        uint16_t reg_mask =
            static_cast<uint16_t>((static_cast<uint16_t>(opcode & 0x0F) << 8U) | static_cast<uint16_t>(low_mask));

        for (int i = 0; i < 12; ++i) {
          if ((reg_mask & (1U << static_cast<unsigned>(i))) != 0) {
            uint32_t val = 0;
            if (space.read_bytes(io_sp, &val, 4)) {
              uint32_t dwarf_reg = static_cast<uint32_t>(dwarf::arm32::r4) + static_cast<uint32_t>(i);
              if (!reg_ctx.write(dwarf_reg, val))
                return false;
              io_sp += 4;
            }
          }
        }
      }
      // 1001nnnn (nnnn != 13,15): vsp = r[nnnn]. Extremely common with
      // frame-pointer-based prologues (e.g. `vsp = r11`), where 0x9D/0x9F
      // are reserved encodings that must be ignored.
      else if ((opcode & 0xF0) == 0x90 && opcode != 0x9D && opcode != 0x9F) {
        uint32_t reg_val = 0;
        uint32_t reg_num = static_cast<uint32_t>(opcode & 0x0F);
        if (!reg_ctx.read(reg_num, reg_val))
          return false;
        io_sp = reg_val;
      }
      // 10100nnn: Pop r4-r[4+nnn]. 10101nnn: Pop r4-r[4+nnn] and r14.
      else if ((opcode & 0xF0) == 0xA0) {
        uint8_t count = opcode & 0x07;
        bool pop_lr = (opcode & 0x08) != 0;

        for (int i = 0; i <= count; ++i, io_sp += 4) {
          uint32_t val = 0;
          if (space.read_bytes(io_sp, &val, 4)) {
            uint32_t dwarf_reg = static_cast<uint32_t>(dwarf::arm32::r4) + static_cast<uint32_t>(i);
            if (!reg_ctx.write(dwarf_reg, val))
              return false;
          }
        }

        if (pop_lr) {
          uint32_t val = 0;
          if (space.read_bytes(io_sp, &val, 4)) {
            if (!reg_ctx.write(static_cast<uint32_t>(dwarf::arm32::r14), val))
              return false;
          }
          io_sp += 4;
        }
      }
      // 10111xxx: Pop VFP double-precision registers d(8) to d(8+xxx)
      else if ((opcode & 0xF8) == 0xB8) {
        uint8_t count = opcode & 0x07;
        for (int i = 0; i <= count; ++i, io_sp += 8) {
          uint64_t val = 0;
          if (space.read_bytes(io_sp, &val, 8)) {
            uint32_t dwarf_reg = static_cast<uint32_t>(dwarf::arm32::d0) + 8U + static_cast<uint32_t>(i);
            if (!reg_ctx.write_raw(dwarf_reg, &val, 8))
              return false;
          }
        }
      }
    }
    return true;
  }
};

} // namespace microfmt
