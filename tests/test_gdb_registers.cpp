// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/inspector/dwarf_abi.hpp>
#include <microfmt/inspector/gdb_registers.hpp>

#include <cstdint>
#include <type_traits>

namespace {

template <typename Traits, typename DwarfTraits> void expect_system_registers_are_mapped() {
  for (const auto &descriptor : DwarfTraits::system_registers) {
    const auto *mapping = Traits::find_by_dwarf(descriptor.index);
    ASSERT_NE(mapping, nullptr) << descriptor.name.data();
  }
}

template <typename Traits> void expect_unique_mappings() {
  uint32_t previous_gdb_index = 0;
  bool has_previous = false;
  const auto verify_layout = [&](auto layout) {
    for (const auto &mapping : layout) {
      if (has_previous) {
        EXPECT_LT(previous_gdb_index, mapping.gdb_index);
      }
      previous_gdb_index = mapping.gdb_index;
      has_previous = true;
      EXPECT_EQ(Traits::find_by_gdb(mapping.gdb_index), &mapping);
      EXPECT_EQ(Traits::find_by_dwarf(mapping.dwarf_index), &mapping);
      EXPECT_EQ(Traits::find_by_name(mapping.name), &mapping);
    }
  };

  verify_layout(Traits::layout());
  verify_layout(Traits::extended_layout());
  verify_layout(Traits::non_standard_layout());
}

static_assert(std::is_same_v<microfmt::arm_abi_traits::gdb_register_traits,
                             microfmt::gdb::register_traits<microfmt::gdb::tags::arm32>>);
static_assert(std::is_same_v<microfmt::aarch64_abi_traits::gdb_register_traits,
                             microfmt::gdb::register_traits<microfmt::gdb::tags::aarch64>>);
static_assert(std::is_same_v<microfmt::riscv32_abi_traits::gdb_register_traits,
                             microfmt::gdb::register_traits<microfmt::gdb::tags::riscv32>>);
static_assert(std::is_same_v<microfmt::riscv64_abi_traits::gdb_register_traits,
                             microfmt::gdb::register_traits<microfmt::gdb::tags::riscv64>>);
static_assert(std::is_same_v<microfmt::x86_abi_traits::gdb_register_traits,
                             microfmt::gdb::register_traits<microfmt::gdb::tags::x86>>);
static_assert(std::is_same_v<microfmt::x86_64_abi_traits::gdb_register_traits,
                             microfmt::gdb::register_traits<microfmt::gdb::tags::x86_64>>);

TEST(GdbRegisterTraits, FindsX86RegistersAcrossNumberingSchemes) {
  using traits = microfmt::gdb::register_traits<microfmt::gdb::tags::x86_64>;

  const auto *gdb_rbx = traits::find_by_gdb(1);
  ASSERT_NE(gdb_rbx, nullptr);
  EXPECT_EQ(gdb_rbx->name, "rbx");
  EXPECT_EQ(gdb_rbx->dwarf_index, microfmt::dwarf::x86_64::rbx);
  EXPECT_EQ(gdb_rbx->bit_size, 64U);
  EXPECT_EQ(gdb_rbx->gdb_type, "int64");

  const auto *dwarf_rdx = traits::find_by_dwarf(microfmt::dwarf::x86_64::rdx);
  ASSERT_NE(dwarf_rdx, nullptr);
  EXPECT_EQ(dwarf_rdx->name, "rdx");
  EXPECT_EQ(dwarf_rdx->gdb_index, 3U);

  const auto *rip = traits::find_by_name("rip");
  ASSERT_NE(rip, nullptr);
  EXPECT_EQ(rip->gdb_index, 16U);
  EXPECT_EQ(rip->dwarf_index, microfmt::dwarf::x86_64::rip);
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
  using x86_64 = microfmt::gdb::register_traits<microfmt::gdb::tags::x86_64>;
  using arm32 = microfmt::gdb::register_traits<microfmt::gdb::tags::arm32>;
  using aarch64 = microfmt::gdb::register_traits<microfmt::gdb::tags::aarch64>;
  using riscv32 = microfmt::gdb::register_traits<microfmt::gdb::tags::riscv32>;
  using riscv64 = microfmt::gdb::register_traits<microfmt::gdb::tags::riscv64>;

  static_assert(x86::layout().size() == 9);
  static_assert(x86_64::layout().size() == 24);
  static_assert(arm32::layout().size() == 17);
  static_assert(aarch64::layout().size() == 34);
  static_assert(riscv32::layout().size() == 33);
  static_assert(riscv64::layout().size() == 33);
  static_assert(x86::extended_layout().size() == 16);
  static_assert(x86_64::extended_layout().size() == 16);
  static_assert(arm32::extended_layout().size() == 32);
  static_assert(aarch64::extended_layout().size() == 32);
  static_assert(riscv32::extended_layout().size() == 32);
  static_assert(riscv64::extended_layout().size() == 32);

  constexpr auto arm32_layout = arm32::layout();
  constexpr auto aarch64_layout = aarch64::layout();
  constexpr auto riscv32_layout = riscv32::layout();
  constexpr auto riscv64_layout = riscv64::layout();

  EXPECT_EQ(arm32_layout.back().name, "cpsr");
  EXPECT_EQ(arm32_layout.back().gdb_index, 25U);
  EXPECT_EQ(aarch64_layout[29].name, "x29");
  EXPECT_EQ(aarch64_layout[29].dwarf_index, microfmt::dwarf::aarch64::fp);
  EXPECT_EQ(riscv32_layout.back().dwarf_index, microfmt::dwarf::riscv::pc);
  EXPECT_EQ(riscv32_layout.back().bit_size, 32U);
  EXPECT_EQ(riscv64_layout.back().dwarf_index, microfmt::dwarf::riscv::pc);
  EXPECT_EQ(riscv64_layout.back().bit_size, 64U);
}

TEST(GdbRegisterTraits, MapsExtendedRegistersForEveryArchitecture) {
  using namespace microfmt;

  EXPECT_EQ(x86_abi_traits::gdb_register_traits::find_by_name("st0")->bit_size, 80U);
  EXPECT_EQ(x86_64_abi_traits::gdb_register_traits::find_by_name("xmm15")->gdb_index, 55U);
  EXPECT_EQ(arm_abi_traits::gdb_register_traits::find_by_name("d31")->gdb_type, "ieee_double");
  EXPECT_EQ(aarch64_abi_traits::gdb_register_traits::find_by_name("v31")->bit_size, 128U);
  EXPECT_EQ(riscv32_abi_traits::gdb_register_traits::find_by_name("f0")->gdb_type, "ieee_single");
  EXPECT_EQ(riscv64_abi_traits::gdb_register_traits::find_by_name("f0")->gdb_type, "ieee_double");
}

TEST(GdbRegisterTraits, MapsEveryNonStandardSystemRegister) {
  using namespace microfmt;

  expect_system_registers_are_mapped<x86_abi_traits::gdb_register_traits, dwarf::x86::register_traits>();
  expect_system_registers_are_mapped<x86_64_abi_traits::gdb_register_traits, dwarf::x86_64::register_traits>();
  expect_system_registers_are_mapped<arm_abi_traits::gdb_register_traits, dwarf::arm32::register_traits>();
  expect_system_registers_are_mapped<aarch64_abi_traits::gdb_register_traits, dwarf::aarch64::register_traits>();
  expect_system_registers_are_mapped<riscv32_abi_traits::gdb_register_traits, dwarf::riscv::register_traits>();
  expect_system_registers_are_mapped<riscv64_abi_traits::gdb_register_traits, dwarf::riscv::register_traits>();
}

TEST(GdbRegisterTraits, AllLayoutsAndLookupsReturnSameUniqueEntries) {
  expect_unique_mappings<microfmt::gdb::register_traits<microfmt::gdb::tags::x86>>();
  expect_unique_mappings<microfmt::gdb::register_traits<microfmt::gdb::tags::x86_64>>();
  expect_unique_mappings<microfmt::gdb::register_traits<microfmt::gdb::tags::arm32>>();
  expect_unique_mappings<microfmt::gdb::register_traits<microfmt::gdb::tags::aarch64>>();
  expect_unique_mappings<microfmt::gdb::register_traits<microfmt::gdb::tags::riscv32>>();
  expect_unique_mappings<microfmt::gdb::register_traits<microfmt::gdb::tags::riscv64>>();
}

} // namespace
