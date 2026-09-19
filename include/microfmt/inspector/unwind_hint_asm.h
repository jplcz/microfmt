// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "../microfmt_config.hpp"

/*
 * Assembler-only helpers for emitting arrays of microfmt::unwind_hint records.
 *
 * A record consists of three pointer-sized fields, in this order:
 *   pc_start, pc_end, routine
 *
 * Include this file from a .S file. When __SIZEOF_POINTER__ is unavailable,
 * define MICROFMT_UNWIND_HINT_POINTER_SIZE to 4 or 8 before including it.
 * The selected size must match the target's uintptr_t and function-pointer
 * representation.
 *
 * Example:
 *
 *   #include <microfmt/inspector/unwind_hint_asm.h>
 *   .section .microfmt.unwind_hints,"a",%progbits
 *   MICROFMT_UNWIND_HINT_TABLE_BEGIN my_hints
 *   MICROFMT_UNWIND_HINT my_start, my_end, my_recover
 *   MICROFMT_UNWIND_HINT_TABLE_END my_hints
 */

#ifndef __ASSEMBLER__
#include <cstddef>
#include <cstdint>

namespace microfmt {
inline constexpr std::size_t unwind_hint_asm_pointer_size = sizeof(uintptr_t);
inline constexpr std::size_t unwind_hint_asm_pc_start_offset = 0;
inline constexpr std::size_t unwind_hint_asm_pc_end_offset = sizeof(uintptr_t);
inline constexpr std::size_t unwind_hint_asm_routine_offset = 2 * sizeof(uintptr_t);
inline constexpr std::size_t unwind_hint_asm_size = 3 * sizeof(uintptr_t);
} // namespace microfmt
#endif

#ifndef MICROFMT_UNWIND_HINT_POINTER_SIZE
# if defined(__SIZEOF_POINTER__)
#  define MICROFMT_UNWIND_HINT_POINTER_SIZE __SIZEOF_POINTER__
# else
#  define MICROFMT_UNWIND_HINT_POINTER_SIZE 4
# endif
#endif

#define MICROFMT_UNWIND_HINT_PC_START_OFFSET 0
#define MICROFMT_UNWIND_HINT_PC_END_OFFSET \
  (MICROFMT_UNWIND_HINT_POINTER_SIZE)
#define MICROFMT_UNWIND_HINT_ROUTINE_OFFSET \
  (2 * MICROFMT_UNWIND_HINT_POINTER_SIZE)
#define MICROFMT_UNWIND_HINT_SIZE \
  (3 * MICROFMT_UNWIND_HINT_POINTER_SIZE)

#if defined(__ASSEMBLER__)
# if MICROFMT_UNWIND_HINT_POINTER_SIZE == 8
#  define MICROFMT_UNWIND_HINT_PTR(value) .quad value
# elif MICROFMT_UNWIND_HINT_POINTER_SIZE == 4
#  define MICROFMT_UNWIND_HINT_PTR(value) .long value
# else
#  error "MICROFMT_UNWIND_HINT_POINTER_SIZE must be 4 or 8"
# endif

#define MICROFMT_UNWIND_HINT_TABLE_BEGIN(name) \
  .balign MICROFMT_UNWIND_HINT_POINTER_SIZE; \
  .global name; \
name:
#define MICROFMT_UNWIND_HINT_TABLE_END(name) \
  .size name, .-name
#define MICROFMT_UNWIND_HINT(pc_start, pc_end, routine) \
  MICROFMT_UNWIND_HINT_PTR(pc_start); \
  MICROFMT_UNWIND_HINT_PTR(pc_end); \
  MICROFMT_UNWIND_HINT_PTR(routine)
#endif
