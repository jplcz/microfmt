// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <microfmt/inspector/dwarf_decoder.hpp>
#include <vector>

namespace {

using bytes = std::vector<uint8_t>;
using decoder = microfmt::dwarf_cfi_decoder<microfmt::aarch64_abi_traits>;

// AArch64 DWARF numbers: x29 (fp) = 29, x30 (lr) = 30, sp = 31.
struct test_registers {
  uintptr_t sp;
  uintptr_t fp;
  uintptr_t lr;
};

bool read_test_register(const void *opaque_state, microfmt::address_space_ref, uint32_t index, void *out_value,
                        size_t value_size) noexcept {
  const auto &regs = *static_cast<const test_registers *>(opaque_state);
  uintptr_t value = 0;
  if (index == 31)
    value = regs.sp;
  else if (index == 29)
    value = regs.fp;
  else if (index == 30)
    value = regs.lr;
  else
    return false;
  if (value_size != sizeof(value))
    return false;
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  std::memcpy(out_value, &value, value_size);
  RELOCO_END_UNSAFE_BUFFER_USAGE
  return true;
}

void put_u32(bytes &b, uint32_t v) {
  for (int i = 0; i < 4; ++i)
    b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void patch_u32(bytes &b, size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i)
    b[at + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
}

void pad_to_4(bytes &b) {
  while (b.size() % 4 != 0)
    b.push_back(0); // DW_CFA_nop
}

// Instructions for: advance 4 bytes; CFA = sp + 16; fp at cfa-16; lr at cfa-8;
// advance 4 bytes; CFA = fp + 16 (def_cfa_register x29).
const bytes kFrameInstructions = {0x41, 0x0e, 0x10, 0x9d, 0x02, 0x9e, 0x01, 0x41, 0x0d, 0x1d};

// .eh_frame CIE: augmentation "zR", FDE pointers pc-relative sdata4, code align 4, data align -8, lr = 30.
bytes eh_cie() {
  bytes b;
  put_u32(b, 0);  // length, patched below
  put_u32(b, 0);  // CIE id
  b.push_back(1); // version
  b.push_back('z');
  b.push_back('R');
  b.push_back(0);    // augmentation string
  b.push_back(4);    // code alignment (ULEB128)
  b.push_back(0x78); // data alignment (SLEB128 -8)
  b.push_back(30);   // return address column
  b.push_back(1);    // augmentation data length
  b.push_back(0x1b); // FDE pointer encoding: pcrel | sdata4
  b.push_back(0x0c); // DW_CFA_def_cfa sp, 0
  b.push_back(31);
  b.push_back(0);
  pad_to_4(b);
  patch_u32(b, 0, static_cast<uint32_t>(b.size() - 4));
  return b;
}

// .eh_frame FDE for [func, func + 0x20). The CIE sits at @p cie_addr.
bytes eh_fde(uintptr_t fde_addr, uintptr_t cie_addr, uintptr_t func, const bytes &insns) {
  bytes b;
  put_u32(b, 0); // length, patched below
  put_u32(b, static_cast<uint32_t>(fde_addr + 4 - cie_addr));
  put_u32(b, static_cast<uint32_t>(static_cast<int32_t>(static_cast<intptr_t>(func - (fde_addr + 8)))));
  put_u32(b, 0x20);
  b.push_back(0); // augmentation data length
  b.insert(b.end(), insns.begin(), insns.end());
  pad_to_4(b);
  patch_u32(b, 0, static_cast<uint32_t>(b.size() - 4));
  return b;
}

struct eh_section {
  std::vector<uint8_t> mem;
  uintptr_t base{0};
  uintptr_t func{0};
};

eh_section make_eh_section(const bytes &insns) {
  eh_section s;
  s.mem.assign(512, 0);
  s.base = reinterpret_cast<uintptr_t>(s.mem.data());
  s.func = s.base + 0x100;
  const bytes cie = eh_cie();
  const bytes fde = eh_fde(s.base + cie.size(), s.base, s.func, insns);
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  std::memcpy(s.mem.data(), cie.data(), cie.size());
  std::memcpy(s.mem.data() + cie.size(), fde.data(), fde.size());
  RELOCO_END_UNSAFE_BUFFER_USAGE
  return s;
}

// Runs the FDE for @p pc on a read-only register snapshot.
bool run_fde(const eh_section &section, bool eh_frame, uintptr_t pc, const test_registers &regs, uintptr_t &fp,
             uintptr_t &ra) {
  std::byte scratch_bytes[sizeof(uintptr_t)]{};
  test_registers snapshot = regs;
  auto registers = microfmt::make_read_only_register_context_ref<read_test_register>(
      snapshot, microfmt::address_space_ref(microfmt::local_space_tag{}), scratch_bytes);
  decoder::scratch_context<8> scratch;
  const uintptr_t fde_addr = section.base + eh_cie().size();
  return decoder::execute_fde(registers, fde_addr, section.base, eh_frame, pc, fp, ra, scratch);
}

void store_u64(uintptr_t addr, uint64_t value) {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  std::memcpy(reinterpret_cast<void *>(addr), &value, sizeof(value));
  RELOCO_END_UNSAFE_BUFFER_USAGE
}

} // namespace

TEST(DwarfCfiDecoder, EhFrameCfaFromStackPointerBeforeFramePointerIsSet) {
  const auto section = make_eh_section(kFrameInstructions);
  std::vector<uint8_t> stack(256, 0);
  const uintptr_t sp = reinterpret_cast<uintptr_t>(stack.data()) + 0x40;
  store_u64(sp, 0x12340);     // saved fp at cfa-16 = sp
  store_u64(sp + 8, 0x56780); // saved lr at cfa-8 = sp + 8

  uintptr_t fp = 0;
  uintptr_t ra = 0;
  ASSERT_TRUE(run_fde(section, true, section.func + 4, test_registers{sp, 0, 0}, fp, ra));
  EXPECT_EQ(fp, 0x12340u);
  EXPECT_EQ(ra, 0x56780u);
}

TEST(DwarfCfiDecoder, EhFrameCfaFromFramePointerAfterDefCfaRegister) {
  const auto section = make_eh_section(kFrameInstructions);
  std::vector<uint8_t> stack(256, 0);
  const uintptr_t fp_reg = reinterpret_cast<uintptr_t>(stack.data()) + 0x80;
  store_u64(fp_reg, 0x9abc0);     // saved fp at cfa-16 = fp
  store_u64(fp_reg + 8, 0xdef00); // saved lr at cfa-8 = fp + 8

  uintptr_t fp = 0;
  uintptr_t ra = 0;
  ASSERT_TRUE(run_fde(section, true, section.func + 12, test_registers{0, fp_reg, 0}, fp, ra));
  EXPECT_EQ(fp, 0x9abc0u);
  EXPECT_EQ(ra, 0xdef00u);
}

TEST(DwarfCfiDecoder, EhFrameRejectsPcOutsideRange) {
  const auto section = make_eh_section(kFrameInstructions);
  std::vector<uint8_t> stack(256, 0);
  const uintptr_t sp = reinterpret_cast<uintptr_t>(stack.data()) + 0x40;
  uintptr_t fp = 0;
  uintptr_t ra = 0;
  EXPECT_FALSE(run_fde(section, true, section.func + 0x40, test_registers{sp, 0, 0}, fp, ra));
}

TEST(DwarfCfiDecoder, EhFrameRejectsUnsupportedOpcodeBeforeTarget) {
  // DW_CFA_def_cfa_expression is not modelled, so the FDE must fail rather than guess the CFA.
  const bytes insns = {0x41, 0x0f, 0x01, 0x00};
  const auto section = make_eh_section(insns);
  std::vector<uint8_t> stack(256, 0);
  const uintptr_t sp = reinterpret_cast<uintptr_t>(stack.data()) + 0x40;
  uintptr_t fp = 0;
  uintptr_t ra = 0;
  EXPECT_FALSE(run_fde(section, true, section.func + 4, test_registers{sp, 0, 0}, fp, ra));
}

TEST(DwarfCfiDecoder, DebugFrameUsesAbsolutePointersAndCieOffset) {
  // CIE version 4 at offset 0, id 0xffffffff; FDE at offset 20 points back to it with offset 0.
  bytes cie;
  put_u32(cie, 0);
  put_u32(cie, 0xFFFFFFFFu);
  cie.push_back(4); // version
  cie.push_back(0); // augmentation string
  cie.push_back(8); // address size
  cie.push_back(0); // segment size
  cie.push_back(4); // code alignment
  cie.push_back(0x78);
  cie.push_back(30);
  cie.push_back(0x0c);
  cie.push_back(31);
  cie.push_back(0);
  pad_to_4(cie);
  patch_u32(cie, 0, static_cast<uint32_t>(cie.size() - 4));

  std::vector<uint8_t> mem(512, 0);
  const uintptr_t base = reinterpret_cast<uintptr_t>(mem.data());
  const uintptr_t func = base + 0x100;
  bytes fde;
  put_u32(fde, 0);
  put_u32(fde, 0); // CIE offset from the section start
  for (int i = 0; i < 8; ++i)
    fde.push_back(static_cast<uint8_t>(func >> (8 * i)));
  for (int i = 0; i < 8; ++i)
    fde.push_back(i == 0 ? 0x20 : 0);
  fde.insert(fde.end(), kFrameInstructions.begin(), kFrameInstructions.end());
  pad_to_4(fde);
  patch_u32(fde, 0, static_cast<uint32_t>(fde.size() - 4));
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  std::memcpy(mem.data(), cie.data(), cie.size());
  std::memcpy(mem.data() + cie.size(), fde.data(), fde.size());
  RELOCO_END_UNSAFE_BUFFER_USAGE

  std::vector<uint8_t> stack(256, 0);
  const uintptr_t sp = reinterpret_cast<uintptr_t>(stack.data()) + 0x40;
  store_u64(sp, 0x12340);
  store_u64(sp + 8, 0x56780);

  std::byte scratch_bytes[sizeof(uintptr_t)]{};
  test_registers snapshot{sp, 0, 0};
  auto registers = microfmt::make_read_only_register_context_ref<read_test_register>(
      snapshot, microfmt::address_space_ref(microfmt::local_space_tag{}), scratch_bytes);
  decoder::scratch_context<8> scratch;
  uintptr_t fp = 0;
  uintptr_t ra = 0;
  ASSERT_TRUE(decoder::execute_fde(registers, base + cie.size(), base, false, func + 4, fp, ra, scratch));
  EXPECT_EQ(fp, 0x12340u);
  EXPECT_EQ(ra, 0x56780u);
}
