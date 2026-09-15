// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

/** @file arm_extab_stream.hpp @brief Sequential multi-word `.ARM.extab`
 * bytecode stream executor. */

#include "address_space.hpp"
#include "arm_exidx_decoder.hpp"
#include <cstdint>

namespace microfmt {

/**
 * @brief Byte cursor pulling opcodes sequentially across `.ARM.extab` words.
 */
class extab_byte_stream {
public:
  /**
   * @brief Constructs a stream rooted at the first extab word.
   * @param space Address space to read from.
   * @param extab_addr Address of the `.ARM.extab` entry.
   */
  constexpr extab_byte_stream(address_space_ref space,
                              uintptr_t extab_addr) noexcept
      : space_(space), current_word_addr_(extab_addr),
        bytes_remaining_in_word_(0) {}

  /**
   * @brief Pulls the next bytecode instruction byte across word boundaries.
   *
   * The first word yields 3 opcode bytes (after the personality byte); each
   * subsequent word yields 4, big-endian.
   *
   * @param out_byte Receives the next opcode byte.
   * @return `true` when a byte was produced, `false` at end of stream or on a
   * read failure.
   */
  bool next_byte(uint8_t &out_byte) noexcept {
    if (bytes_remaining_in_word_ == 0) {
      uint32_t word = 0;
      if (!space_.read_bytes(current_word_addr_, &word, 4)) {
        return false;
      }
      current_word_addr_ += 4;

      if (is_first_word_) {
        is_first_word_ = false;
        // Extab Word 0: [Personality Index (8 bits) | Opcode Byte 1 (8 bits) |
        // Opcode Byte 2 (8 bits) | Opcode Byte 3 (8 bits)]
        current_word_buffer_[0] = static_cast<uint8_t>((word >> 16) & 0xFF);
        current_word_buffer_[1] = static_cast<uint8_t>((word >> 8) & 0xFF);
        current_word_buffer_[2] = static_cast<uint8_t>(word & 0xFF);
        buffer_idx_ = 0;
        bytes_remaining_in_word_ = 3;
      } else {
        // Subsequent Extab Words: 4 sequential opcode bytes packed big-endian
        current_word_buffer_[0] = static_cast<uint8_t>((word >> 24) & 0xFF);
        current_word_buffer_[1] = static_cast<uint8_t>((word >> 16) & 0xFF);
        current_word_buffer_[2] = static_cast<uint8_t>((word >> 8) & 0xFF);
        current_word_buffer_[3] = static_cast<uint8_t>(word & 0xFF);
        buffer_idx_ = 0;
        bytes_remaining_in_word_ = 4;
      }
    }

    if (bytes_remaining_in_word_ > 0) {
      out_byte = current_word_buffer_[buffer_idx_++];
      bytes_remaining_in_word_--;
      return true;
    }
    return false;
  }

private:
  address_space_ref space_;
  uintptr_t current_word_addr_;
  uint8_t current_word_buffer_[4]{};
  uint8_t buffer_idx_{0};
  uint8_t bytes_remaining_in_word_{0};
  bool is_first_word_{true};
};

// ============================================================================
// Multi-Word Extab Bytecode Stream Executor
// ============================================================================

/**
 * @brief Executes a multi-word `.ARM.extab` bytecode program.
 */
class extab_stream_executor {
public:
  /**
   * @brief Runs the extab bytecode stream, updating SP and writing restored
   * registers.
   *
   * Supports `vsp` add/sub adjustments, FINISH, integer-register pops, and
   * VFP double-precision register pops.
   *
   * @param space Address space to read stack values from.
   * @param extab_addr Address of the `.ARM.extab` entry.
   * @param io_sp In/out: virtual stack pointer.
   * @param reg_ctx Target register context handle to write recovered registers
   * into.
   * @return `true` when the stream was consumed successfully.
   */
  static bool execute(address_space_ref space, uintptr_t extab_addr,
                      uintptr_t &io_sp, register_context_ref reg_ctx) noexcept {
    extab_byte_stream stream(space, extab_addr);
    uint8_t opcode = 0;

    while (stream.next_byte(opcode)) {
      // 0xB0 = FINISH opcode (terminate unwinding bytecode block)
      if (opcode == 0xB0) {
        return true;
      }

      // 00xxxxxx: vsp = vsp + (xxxxxx << 2) + 4
      if ((opcode & 0xC0) == 0x00) {
        uint32_t offset = ((opcode & 0x3F) << 2) + 4;
        io_sp += offset;
      }
      // 01xxxxxx: vsp = vsp - (xxxxxx << 2) - 4 (sub sp adjustment)
      else if ((opcode & 0xC0) == 0x40) {
        uint32_t offset = ((opcode & 0x3F) << 2) + 4;
        io_sp -= offset;
      }
      // 1000iiii iiiiiiii: Pop integer registers r4-r15
      else if ((opcode & 0xF0) == 0x80) {
        uint8_t opcode_low = 0;
        if (!stream.next_byte(opcode_low))
          break;
        uint16_t reg_mask =
            (static_cast<uint16_t>(opcode & 0x0F) << 8) | opcode_low;

        for (int i = 0; i < 12; ++i) {
          if ((reg_mask & (1 << i)) != 0) {
            uint32_t val = 0;
            if (space.read_bytes(io_sp, &val, 4)) {
              uint32_t dwarf_reg = dwarf::arm32::R4 + i;
              if (!reg_ctx.write(dwarf_reg, val))
                return false;
              io_sp += 4;
            }
          }
        }
      }
      // 10111xxx: Pop VFP double-precision registers d(8) to d(8+xxx)
      else if ((opcode & 0xF8) == 0xB8) {
        uint8_t count = opcode & 0x07;
        for (int i = 0; i <= count; ++i, io_sp += 8) {
          uint64_t val = 0;
          if (space.read_bytes(io_sp, &val, 8)) {
            uint32_t dwarf_reg = dwarf::arm32::D0 + 8 + i;
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