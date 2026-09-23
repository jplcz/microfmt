// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file arm_exidx_search.hpp @brief Binary search over sorted `.ARM.exidx`
 * unwind tables. */

#include "address_space.hpp"
#include <cstdint>

namespace microfmt {

/**
 * @brief Binary search over sorted `.ARM.exidx` unwind tables.
 */
class MICROFMT_API_CLASS exidx_table_searcher {
public:
  /**
   * @brief Decodes a 31-bit program-relative (PREL31) offset.
   * @param entry_addr Address of the PREL31 word itself (base).
   * @param prel31 Raw PREL31 encoding.
   * @return Absolute address computed from the entry location.
   */
  [[nodiscard]] static constexpr uintptr_t
  decode_prel31(uintptr_t entry_addr, uint32_t prel31) noexcept {
    // PREL31 is a 31-bit signed offset relative to the address of the prel31
    // word itself. Arithmetic right shift sign-extends from bit 30 to
    // 32-bit/64-bit int32_t.
    int32_t signed_offset = static_cast<int32_t>(prel31 << 1) >> 1;
    return detail::add_address_offset(entry_addr, signed_offset);
  }

  /**
   * @brief Locates the unwind descriptor covering target_pc.
   *
   * Performs a zero-allocation binary search over the sorted `.ARM.exidx`
   * table, matching the highest function start not greater than @p target_pc.
   *
   * @param space Address space to read from.
   * @param table_base Base address of the `.ARM.exidx` table.
   * @param num_entries Number of 8-byte table entries.
   * @param target_pc PC being looked up.
   * @param out_unwind_data Receives the descriptor (inline bytecode or
   * `.ARM.extab` pointer).
   * @return `true` when a non-`EXIDX_CANTUNWIND` entry matched.
   */
  [[nodiscard]] static bool
  find_exidx_entry(address_space_ref space, uintptr_t table_base,
                   size_t num_entries, uintptr_t target_pc,
                   uint32_t &out_unwind_data) noexcept {
    if (num_entries == 0 || table_base == 0)
      return false;

    size_t low = 0;
    size_t high = num_entries;
    size_t match_index = num_entries;

    // Iterative binary search: find the highest function address <= target_pc
    while (low < high) {
      size_t mid = low + (high - low) / 2;
      uintptr_t entry_addr =
          table_base + (mid * 8); // Each entry is 2 words (8 bytes)

      uint32_t prel31 = 0;
      if (!space.read_bytes(entry_addr, &prel31, 4)) {
        return false;
      }

      uintptr_t fn_addr = decode_prel31(entry_addr, prel31);

      if (fn_addr <= target_pc) {
        match_index = mid; // Candidate found, try looking for a closer (higher)
                           // function start
        low = mid + 1;
      } else {
        high = mid;
      }
    }

    if (match_index == num_entries) {
      return false; // Target PC precedes all entries in the table
    }

    // Read the second word (unwind descriptor or pointer to .ARM.extab)
    uintptr_t matched_entry_addr = table_base + (match_index * 8);
    uint32_t unwind_data = 0;
    if (!space.read_bytes(matched_entry_addr + 4, &unwind_data, 4)) {
      return false;
    }

    // Check for EXIDX_CANTUNWIND sentinel value (0x1)
    if (unwind_data == 0x1) {
      return false;
    }

    out_unwind_data = unwind_data;
    return true;
  }
};

} // namespace microfmt