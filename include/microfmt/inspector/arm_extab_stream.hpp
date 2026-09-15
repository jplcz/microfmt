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

class extab_byte_stream {
public:
  constexpr extab_byte_stream(address_space_ref space,
                              uintptr_t extab_addr) noexcept
      : space_(space), current_word_addr_(extab_addr),
        bytes_remaining_in_word_(0) {}

  // Pulls the next bytecode instruction byte sequentially across word
  // boundaries
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

class extab_stream_executor {
public:
  static bool execute(address_space_ref space, uintptr_t extab_addr,
                      uintptr_t &io_sp, uintptr_t &,
                      arm_register_state &out_regs) noexcept {
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
              out_regs.r[4 + i] = val;
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
            out_regs.d[8 + i] = static_cast<uint32_t>(val & 0xFFFFFFFF);
          }
        }
      }
    }
    return true;
  }
};

} // namespace microfmt