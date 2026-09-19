// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file arm_extab_stream.hpp @brief Sequential multi-word `.ARM.extab`
 * bytecode stream executor. */

#include "address_space.hpp"
#include "arm_exidx_decoder.hpp"
#include <cstdint>

namespace microfmt {

/**
 * @brief Byte cursor pulling opcodes sequentially across `.ARM.extab` words.
 *
 * Per ARM EHABI ("Personality routine exception-handling table entries"),
 * word 0's most-significant byte packs `1 <personality-index(3 bits)>
 * <reserved(4 bits)>`-like fields: bit 31 set marks an inline (rather than
 * out-of-line personality-routine-pointer) table entry, and bits 27:24 select
 * one of the three ABI personality routines, which in turn determines how
 * many opcode bytes word 0 contributes and whether a word count follows:
 *   - Personality 0 (`Su16`, *Short*): word 0 = `[1|idx=0][opcode][opcode]
 *     [opcode]` -- 3 opcode bytes, no extra-word count.
 *   - Personality 1/2 (`Lu16`/`Lu32`, *Long*): word 0 = `[1|idx][N: extra
 *     word count][opcode][opcode]` -- only 2 opcode bytes from word 0,
 *     followed by @c N additional words each contributing 4 more opcode
 *     bytes (MSB first).
 */
class extab_byte_stream {
public:
  /**
   * @brief Constructs a stream rooted at the first extab word.
   * @param space Address space to read from.
   * @param extab_addr Address of the `.ARM.extab` entry.
   */
  constexpr extab_byte_stream(address_space_ref space, uintptr_t extab_addr) noexcept
      : space_(space), current_word_addr_(extab_addr), bytes_remaining_in_word_(0) {}

  /**
   * @brief Reports whether the stream failed to parse a supported word-0
   * header (e.g. an out-of-line personality-routine pointer, which cannot be
   * interpreted as bytecode).
   */
  [[nodiscard]] constexpr bool failed() const noexcept { return failed_; }

  /**
   * @brief Pulls the next bytecode instruction byte across word boundaries.
   *
   * @param out_byte Receives the next opcode byte.
   * @return `true` when a byte was produced, `false` at end of stream or on a
   * read failure.
   */
  bool next_byte(uint8_t &out_byte) noexcept {
    if (failed_)
      return false;

    if (bytes_remaining_in_word_ == 0) {
      if (words_remaining_ == 0)
        return false;

      uint32_t word = 0;
      if (!space_.read_bytes(current_word_addr_, &word, 4)) {
        return false;
      }
      current_word_addr_ += 4;
      --words_remaining_;

      if (is_first_word_) {
        is_first_word_ = false;

        // Bit 31 clear means this word is instead a PREL31 pointer to an
        // out-of-line personality-routine function -- not bytecode we can
        // execute.
        if ((word & 0x80000000U) == 0U) {
          failed_ = true;
          return false;
        }

        const uint8_t personality_index = static_cast<uint8_t>((word >> 24) & 0x7FU);
        buffer_idx_ = 0;

        if (personality_index == 0) {
          // Personality 0 (Su16, Short): 3 opcode bytes packed into bits
          // 23:16, 15:8, 7:0.
          current_word_buffer_[0] = static_cast<uint8_t>((word >> 16) & 0xFF);
          current_word_buffer_[1] = static_cast<uint8_t>((word >> 8) & 0xFF);
          current_word_buffer_[2] = static_cast<uint8_t>(word & 0xFF);
          bytes_remaining_in_word_ = 3;
        } else {
          // Personality 1/2 (Lu16/Lu32, Long): bits 23:16 hold the count N
          // of additional 4-byte words carrying more opcode bytes; only 2
          // opcode bytes come from this word (bits 15:8, 7:0).
          words_remaining_ = static_cast<uint8_t>((word >> 16) & 0xFF);
          current_word_buffer_[0] = static_cast<uint8_t>((word >> 8) & 0xFF);
          current_word_buffer_[1] = static_cast<uint8_t>(word & 0xFF);
          bytes_remaining_in_word_ = 2;
        }
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
      RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

      RELOCO_DEBUG_ASSERT(buffer_idx_ < std::size(current_word_buffer_), "Index out of range");
      out_byte = current_word_buffer_[buffer_idx_++];

      RELOCO_END_UNSAFE_BUFFER_USAGE;

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
  bool failed_{false};
  /// Words left to consume: word 0, plus (for personality 1/2) `N`
  /// additional words discovered while parsing word 0.
  uint32_t words_remaining_{1};
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
  static bool execute(address_space_ref space, uintptr_t extab_addr, uintptr_t &io_sp,
                      register_context_ref reg_ctx) noexcept {
    extab_byte_stream stream(space, extab_addr);
    uint8_t opcode = 0;

    while (stream.next_byte(opcode)) {
      // 0xB0 = FINISH opcode (terminate unwinding bytecode block)
      if (opcode == 0xB0) {
        return true;
      }

      // 00xxxxxx: vsp = vsp + (xxxxxx << 2) + 4
      if ((opcode & 0xC0) == 0x00) {
        uint32_t offset = (static_cast<uint32_t>(opcode & 0x3F) << 2U) + 4U;
        io_sp += offset;
      }
      // 01xxxxxx: vsp = vsp - (xxxxxx << 2) - 4 (sub sp adjustment)
      else if ((opcode & 0xC0) == 0x40) {
        uint32_t offset = (static_cast<uint32_t>(opcode & 0x3F) << 2U) + 4U;
        io_sp -= offset;
      }
      // 1000iiii iiiiiiii: Pop integer registers r4-r15
      else if ((opcode & 0xF0) == 0x80) {
        uint8_t opcode_low = 0;
        if (!stream.next_byte(opcode_low))
          break;
        uint16_t reg_mask =
            static_cast<uint16_t>((static_cast<uint16_t>(opcode & 0x0F) << 8U) | static_cast<uint16_t>(opcode_low));

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
      // frame-pointer-based prologues (e.g. `vsp = r11`).
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
    return !stream.failed();
  }
};

} // namespace microfmt