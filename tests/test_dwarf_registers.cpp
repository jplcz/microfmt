// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#define RAX 0xBAD
#define FP 0xBAD
#define LR 0xBAD
#define PC 0xBAD
#define SP 0xBAD
#define X0 0xBAD

#include <microfmt/inspector/dwarf_registers.hpp>

#undef RAX
#undef FP
#undef LR
#undef PC
#undef SP
#undef X0

#include <gtest/gtest.h>

TEST(DwarfRegisters, AvoidsUppercaseRegisterMacroCollisions) {
  EXPECT_EQ(microfmt::dwarf::x86_64::rax, 0U);
  EXPECT_EQ(microfmt::dwarf::x86_64::fp, 6U);
  EXPECT_EQ(microfmt::dwarf::arm32::lr, 14U);
  EXPECT_EQ(microfmt::dwarf::aarch64::x0, 0U);
  EXPECT_EQ(microfmt::dwarf::aarch64::sp, 31U);
  EXPECT_EQ(microfmt::dwarf::riscv::pc, 65U);
}
