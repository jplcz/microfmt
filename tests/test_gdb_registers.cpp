// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/inspector/dwarf_abi.hpp>
#include <microfmt/inspector/gdb_registers.hpp>

#include <cstdint>
#include <type_traits>

namespace {

static_assert(std::is_same_v<
              microfmt::arm_abi_traits::gdb_register_traits,
              microfmt::gdb::register_traits<microfmt::gdb::tags::arm32>>);
static_assert(std::is_same_v<
              microfmt::aarch64_abi_traits::gdb_register_traits,
              microfmt::gdb::register_traits<microfmt::gdb::tags::aarch64>>);
static_assert(std::is_same_v<
              microfmt::riscv32_abi_traits::gdb_register_traits,
              microfmt::gdb::register_traits<microfmt::gdb::tags::riscv32>>);
static_assert(std::is_same_v<
              microfmt::riscv64_abi_traits::gdb_register_traits,
              microfmt::gdb::register_traits<microfmt::gdb::tags::riscv64>>);
static_assert(std::is_same_v<
              microfmt::x86_abi_traits::gdb_register_traits,
              microfmt::gdb::register_traits<microfmt::gdb::tags::x86>>);
static_assert(std::is_same_v<
              microfmt::x86_64_abi_traits::gdb_register_traits,
              microfmt::gdb::register_traits<microfmt::gdb::tags::x86_64>>);

TEST(GdbRegisterTraits, FindsX86RegistersAcrossNumberingSchemes) {
  using traits = microfmt::gdb::register_traits<microfmt::gdb::tags::x86_64>;

  const auto *gdb_rbx = traits::find_by_gdb(1);
  ASSERT_NE(gdb_rbx, nullptr);
  EXPECT_EQ(gdb_rbx->name, "rbx");
  EXPECT_EQ(gdb_rbx->dwarf_index, microfmt::dwarf::x86_64::RBX);
  EXPECT_EQ(gdb_rbx->bit_size, 64U);
  EXPECT_EQ(gdb_rbx->gdb_type, "int64");

  const auto *dwarf_rdx =
      traits::find_by_dwarf(microfmt::dwarf::x86_64::RDX);
  ASSERT_NE(dwarf_rdx, nullptr);
  EXPECT_EQ(dwarf_rdx->name, "rdx");
  EXPECT_EQ(dwarf_rdx->gdb_index, 3U);

  const auto *rip = traits::find_by_name("rip");
  ASSERT_NE(rip, nullptr);
  EXPECT_EQ(rip->gdb_index, 16U);
  EXPECT_EQ(rip->dwarf_index, microfmt::dwarf::x86_64::RIP);
  EXPECT_EQ(rip->gdb_type, "code_ptr");
}

TEST(GdbRegisterTraits, ReportsUnknownRegisters) {
  using traits = microfmt::gdb::register_traits<microfmt::gdb::tags::arm32>;

  EXPECT_EQ(traits::find_by_gdb(24), nullptr);
  EXPECT_EQ(traits::find_by_dwarf(UINT32_MAX), nullptr);
  EXPECT_EQ(traits::find_by_name("R0"), nullptr);
  EXPECT_EQ(traits::find_by_name(""), nullptr);
}

TEST(GdbRegisterTraits, ExposesExpectedArchitectureLayouts) {
  using x86 = microfmt::gdb::register_traits<microfmt::gdb::tags::x86>;
  using x86_64 =
      microfmt::gdb::register_traits<microfmt::gdb::tags::x86_64>;
  using arm32 = microfmt::gdb::register_traits<microfmt::gdb::tags::arm32>;
  using aarch64 =
      microfmt::gdb::register_traits<microfmt::gdb::tags::aarch64>;
  using riscv32 =
      microfmt::gdb::register_traits<microfmt::gdb::tags::riscv32>;
  using riscv64 =
      microfmt::gdb::register_traits<microfmt::gdb::tags::riscv64>;

  static_assert(x86::layout().size() == 9);
  static_assert(x86_64::layout().size() == 24);
  static_assert(arm32::layout().size() == 17);
  static_assert(aarch64::layout().size() == 34);
  static_assert(riscv32::layout().size() == 33);
  static_assert(riscv64::layout().size() == 33);

  constexpr auto arm32_layout = arm32::layout();
  constexpr auto aarch64_layout = aarch64::layout();
  constexpr auto riscv32_layout = riscv32::layout();
  constexpr auto riscv64_layout = riscv64::layout();

  EXPECT_EQ(arm32_layout.back().name, "cpsr");
  EXPECT_EQ(arm32_layout.back().gdb_index, 25U);
  EXPECT_EQ(aarch64_layout[29].name, "x29");
  EXPECT_EQ(aarch64_layout[29].dwarf_index, microfmt::dwarf::aarch64::FP);
  EXPECT_EQ(riscv32_layout.back().dwarf_index, microfmt::dwarf::riscv::PC);
  EXPECT_EQ(riscv32_layout.back().bit_size, 32U);
  EXPECT_EQ(riscv64_layout.back().dwarf_index, microfmt::dwarf::riscv::PC);
  EXPECT_EQ(riscv64_layout.back().bit_size, 64U);
}

TEST(GdbRegisterTraits, LayoutAndLookupReturnSameEntries) {
  using traits =
      microfmt::gdb::register_traits<microfmt::gdb::tags::aarch64>;

  for (const auto &mapping : traits::layout()) {
    EXPECT_EQ(traits::find_by_gdb(mapping.gdb_index), &mapping);
    EXPECT_EQ(traits::find_by_dwarf(mapping.dwarf_index), &mapping);
    EXPECT_EQ(traits::find_by_name(mapping.name), &mapping);
  }
}

} // namespace
