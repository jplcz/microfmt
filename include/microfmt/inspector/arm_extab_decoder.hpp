// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file arm_extab_decoder.hpp @brief `.ARM.extab` unwind descriptor
 * resolution. */

#include "arm_exidx_decoder.hpp"
#include "arm_exidx_search.hpp"
#include <cstdint>

namespace microfmt {

/**
 * @brief Resolves whether unwind instructions are inline or stored in an
 * external `.ARM.extab` entry.
 */
class extab_entry_resolver {
public:
  /**
   * @brief Resolves the executable unwind descriptor for an EXIDX entry.
   *
   * Follows PREL31 pointers into `.ARM.extab` when bit 31 is set; otherwise
   * passes the inline descriptor through unchanged.
   *
   * @param space Address space to read from.
   * @param exidx_word2_addr Address of the EXIDX entry's second word.
   * @param raw_unwind_data Raw second-word contents.
   * @param out_unwind_word Receives the executable descriptor or bytecode.
   * @return `true` on success, `false` for the `EXIDX_CANTUNWIND` sentinel or
   * when reading the extab entry fails.
   */
  [[nodiscard]] static bool
  resolve_unwind_word(address_space_ref space, uintptr_t exidx_word2_addr,
                      uint32_t raw_unwind_data,
                      uint32_t &out_unwind_word) noexcept {
    // Check for sentinel value (Cannot unwind)
    if (raw_unwind_data == 0x1) {
      return false;
    }

    // Check Bit 31: If set, it's a PREL31 offset pointing to an external
    // .ARM.extab entry
    if ((raw_unwind_data & 0x80000000U) != 0U) {
      // Compute absolute address of the extab entry using PREL31 decoding
      uintptr_t extab_entry_addr = exidx_table_searcher::decode_prel31(
          exidx_word2_addr, raw_unwind_data);

      // Read the first word of the .ARM.extab entry
      // Typically, an extab entry starts with a personality routine word or
      // direct bytecode descriptor
      uint32_t extab_word0 = 0;
      if (!space.read_bytes(extab_entry_addr, &extab_word0, 4)) {
        return false;
      }

      // If the extab word points to a personality routine address (often a
      // pointer with bit 31 set or clear depending on linking), or contains
      // inline bytecode words following it, we capture the primary descriptor
      // word:
      out_unwind_word = extab_word0;
      return true;
    }

    // Bit 31 is clear: the exidx word itself contains inline bytecode /
    // personality data
    out_unwind_word = raw_unwind_data;
    return true;
  }
};

} // namespace microfmt