// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/coredump/arch_aarch64.hpp>
#include <microfmt/coredump/arch_arm_cortex_a.hpp>
#include <microfmt/coredump/arch_arm_cortex_m.hpp>
#include <microfmt/coredump/arch_riscv.hpp>
#include <microfmt/coredump/arch_x86_32.hpp>
#include <microfmt/coredump/arch_x86_64.hpp>
#include <microfmt/coredump/elf_core_writer.hpp>
#include <microfmt/coredump/framed_sink.hpp>
#include <microfmt/coredump/register_sets.hpp>
#include <microfmt/microfmt.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

namespace cd = microfmt::coredump;

constexpr uint64_t base_address = 0x10000;
constexpr uint64_t page = 4096;

struct fake_core_tag {};

struct fake_core_memory {
  uint64_t base{base_address};
  uint64_t pages{8};
  int64_t hole_page{3};
  int64_t zero_page{-1};
  bool fail_bulk_reads{false};
};

uint8_t pattern_byte(uint64_t address) { return static_cast<uint8_t>((address * 31U + 7U) & 0xFFU); }

} // namespace

template <> struct microfmt::address_space_traits<fake_core_tag> {
  using context_type = fake_core_memory;

  static bool read_bytes(microfmt::value_ref<const context_type> context, uintptr_t address, void *destination,
                         size_t size) noexcept {
    if (address < context->base || address + size > context->base + context->pages * page)
      return false;
    if (size > 1 && context->fail_bulk_reads)
      return false;
    const microfmt::span<uint8_t> bytes(static_cast<uint8_t *>(destination), size);
    for (size_t i = 0; i < size; ++i) {
      const uint64_t current = address + i;
      if (static_cast<int64_t>((current - context->base) / page) == context->hole_page)
        return false;
      bytes[i] =
          static_cast<int64_t>((current - context->base) / page) == context->zero_page ? 0 : pattern_byte(current);
    }
    return true;
  }

  static bool read_string(microfmt::value_ref<const context_type>, uintptr_t, char *, size_t, size_t &,
                          bool &) noexcept {
    return false;
  }
};

namespace {

struct fake_registers {
  uint64_t rip{0x401000};
  uint64_t rsp{0x7fff0000};
};

bool read_fake_register(const void *opaque, microfmt::address_space_ref, uint32_t index, void *out,
                        size_t size) noexcept {
  const auto &regs = *static_cast<const fake_registers *>(opaque);
  if (size != sizeof(uint64_t))
    return false;
  uint64_t value = 0;
  if (index == 16)
    value = regs.rip;
  else if (index == 7)
    value = regs.rsp;
  else
    return false; // every other register is unreadable and must be stored as zero
  *static_cast<uint64_t *>(out) = value;
  return true;
}

uint64_t load_le(const std::vector<char> &data, size_t offset, size_t bytes) {
  uint64_t value = 0;
  for (size_t i = 0; i < bytes; ++i)
    value |= static_cast<uint64_t>(static_cast<uint8_t>(data.at(offset + i))) << (8U * i);
  return value;
}

struct core_fixture {
  fake_core_memory memory;
  fake_registers registers;
  std::byte register_scratch[8]{};
  std::vector<cd::core_region> regions{{base_address, 8 * page, cd::pf_r | cd::pf_w}};
  std::vector<cd::core_thread> threads;
  std::vector<cd::core_mapped_file> files;
  std::vector<cd::core_auxv_entry> auxv;
  bool skip_zero_pages{false};
  microfmt::array<char, 1024> chunk{};

  explicit core_fixture(bool with_file = true) {
    microfmt::address_space<fake_core_tag> space(memory);
    threads.push_back({});
    threads[0].id = 42;
    threads[0].signal = 11;
    threads[0].regs =
        microfmt::make_read_only_register_context_ref<read_fake_register>(registers, space.ref(), register_scratch);
    if (with_file)
      files.push_back({base_address, base_address + 4 * page, 0, "/bin/app"});
  }

  std::vector<char> dump(cd::core_plan &plan, cd::core_stats &stats, uint64_t budget = ~uint64_t{0}) {
    const microfmt::address_space<fake_core_tag> space(memory);
    const cd::span_core_source source(regions, threads, files, auxv);
    const cd::elf_core_writer<cd::arch_x86_64, cd::span_core_source> writer(source, space.ref(), chunk, budget,
                                                                            skip_zero_pages);
    plan = writer.plan();
    std::vector<char> out;
    auto append = [&out](microfmt::string_view text) { out.insert(out.end(), text.begin(), text.end()); };
    microfmt::callback_sink<decltype(append)> callback(append);
    stats = writer.write(callback.as_sink(), plan);
    return out;
  }
};

TEST(ElfCoreWriter, PlanMatchesBytesAndHeaderIsConsistent) {
  core_fixture fixture;
  cd::core_plan plan;
  cd::core_stats stats;
  const auto out = fixture.dump(plan, stats);

  ASSERT_TRUE(plan.valid);
  EXPECT_EQ(out.size(), plan.total_size);
  EXPECT_EQ(stats.bytes_written, plan.total_size);
  EXPECT_EQ(stats.read_failures, 0U);
  EXPECT_FALSE(stats.plan_mismatch);

  EXPECT_EQ(load_le(out, 0, 4), 0x464c457fU); // 0x7f 'E' 'L' 'F'
  EXPECT_EQ(load_le(out, 4, 1), 2U);          // 64-bit
  EXPECT_EQ(load_le(out, 5, 1), 1U);          // little endian
  EXPECT_EQ(load_le(out, 16, 2), 4U);         // ET_CORE
  EXPECT_EQ(load_le(out, 18, 2), 62U);
  EXPECT_EQ(load_le(out, 32, 8), 64U); // program headers follow the header
  EXPECT_EQ(load_le(out, 56, 2), 3U);  // note + two loads (hole splits the region)

  EXPECT_EQ(plan.load_segments, 2U);
  EXPECT_EQ(plan.segment_bytes, 7 * page);
  EXPECT_EQ(plan.threads, 1U);
  EXPECT_EQ(plan.files, 1U);
}

TEST(ElfCoreWriter, LoadSegmentsCarryReadableMemoryOnly) {
  core_fixture fixture;
  cd::core_plan plan;
  cd::core_stats stats;
  const auto out = fixture.dump(plan, stats);

  const size_t note_header = 64;
  const size_t first_load = note_header + 56;
  const size_t second_load = first_load + 56;
  EXPECT_EQ(load_le(out, note_header, 4), 4U);    // PT_NOTE
  EXPECT_EQ(load_le(out, first_load, 4), 1U);     // PT_LOAD
  EXPECT_EQ(load_le(out, first_load + 4, 4), 6U); // rw
  EXPECT_EQ(load_le(out, first_load + 16, 8), base_address);
  EXPECT_EQ(load_le(out, first_load + 32, 8), 3 * page);
  EXPECT_EQ(load_le(out, second_load + 16, 8), base_address + 4 * page);
  EXPECT_EQ(load_le(out, second_load + 32, 8), 4 * page);

  const size_t first_offset = load_le(out, first_load + 8, 8);
  const size_t second_offset = load_le(out, second_load + 8, 8);
  EXPECT_EQ(second_offset, first_offset + 3 * page);
  EXPECT_EQ(static_cast<uint8_t>(out.at(first_offset + 5)), pattern_byte(base_address + 5));
  EXPECT_EQ(static_cast<uint8_t>(out.at(second_offset + 9)), pattern_byte(base_address + 4 * page + 9));
  EXPECT_EQ(first_offset + 7 * page, out.size());
}

TEST(ElfCoreWriter, PrstatusAndFileNotes) {
  core_fixture fixture;
  cd::core_plan plan;
  cd::core_stats stats;
  const auto out = fixture.dump(plan, stats);

  const size_t notes = load_le(out, 64 + 8, 8);
  EXPECT_EQ(load_le(out, notes, 4), 5U);
  EXPECT_EQ(load_le(out, notes + 4, 4), 336U);
  EXPECT_EQ(load_le(out, notes + 8, 4), 1U);           // NT_PRSTATUS
  EXPECT_EQ(load_le(out, notes + 12, 4), 0x45524f43U); // "CORE"
  const size_t body = notes + 20;
  EXPECT_EQ(load_le(out, body, 4), 11U);                        // si_signo
  EXPECT_EQ(load_le(out, body + 32, 4), 42U);                   // pid
  EXPECT_EQ(load_le(out, body + 112 + 8 * 16, 8), 0x401000U);   // rip
  EXPECT_EQ(load_le(out, body + 112 + 8 * 19, 8), 0x7fff0000U); // rsp
  EXPECT_EQ(load_le(out, body + 112 + 8 * 10, 8), 0U);          // rax unreadable

  const size_t file_note = body + 336;
  EXPECT_EQ(load_le(out, file_note + 8, 4), 0x46494c45U);
  const size_t file_body = file_note + 20;
  EXPECT_EQ(load_le(out, file_body, 8), 1U);
  EXPECT_EQ(load_le(out, file_body + 8, 8), page);
  EXPECT_EQ(load_le(out, file_body + 16, 8), base_address);
  EXPECT_EQ(load_le(out, file_body + 24, 8), base_address + 4 * page);
  EXPECT_EQ(load_le(out, file_body + 32, 8), 0U);
  EXPECT_STREQ(&out.at(file_body + 40), "/bin/app");
  EXPECT_EQ(file_note + 20 + ((40 + 9 + 3) & ~size_t{3}), plan.note_bytes + notes);
}

TEST(ElfCoreWriter, NoFilesMeansNoFileNote) {
  core_fixture fixture(false);
  cd::core_plan plan;
  cd::core_stats stats;
  const auto out = fixture.dump(plan, stats);
  EXPECT_EQ(plan.files, 0U);
  EXPECT_EQ(plan.note_bytes, 20U + 336U);
  EXPECT_EQ(out.size(), plan.total_size);
}

TEST(ElfCoreWriter, BudgetTruncatesMemoryButKeepsHeadersAndNotes) {
  core_fixture fixture;
  cd::core_plan plan;
  cd::core_stats stats;
  const auto out = fixture.dump(plan, stats, 5000);
  EXPECT_EQ(plan.segment_bytes, 5000U);
  EXPECT_EQ(plan.load_segments, 1U);
  EXPECT_EQ(out.size(), plan.total_size);
  EXPECT_FALSE(stats.plan_mismatch);
}

TEST(ElfCoreWriter, OutputIsDeterministic) {
  core_fixture fixture;
  cd::core_plan plan_a, plan_b;
  cd::core_stats stats;
  EXPECT_EQ(fixture.dump(plan_a, stats), fixture.dump(plan_b, stats));
}

TEST(ElfCoreWriter, FailedReadsBecomeZerosAndAreCounted) {
  core_fixture fixture;
  fixture.memory.fail_bulk_reads = true;
  cd::core_plan plan;
  cd::core_stats stats;
  const auto out = fixture.dump(plan, stats);
  EXPECT_EQ(out.size(), plan.total_size);
  EXPECT_GT(stats.read_failures, 0U);
  EXPECT_EQ(static_cast<uint8_t>(out.at(out.size() - 1)), 0U);
}

TEST(ElfCoreWriter, ChangedTargetKeepsSizeAndReportsMismatch) {
  core_fixture fixture;
  cd::core_plan plan;
  cd::core_stats stats;
  fixture.dump(plan, stats);

  fixture.memory.hole_page = -1; // the hole disappeared after planning, so the layout no longer matches
  const microfmt::address_space<fake_core_tag> space(fixture.memory);
  const cd::span_core_source source(fixture.regions, fixture.threads, fixture.files);
  const cd::elf_core_writer<cd::arch_x86_64, cd::span_core_source> writer(source, space.ref(), fixture.chunk);
  std::vector<char> out;
  auto append = [&out](microfmt::string_view text) { out.insert(out.end(), text.begin(), text.end()); };
  microfmt::callback_sink<decltype(append)> callback(append);
  stats = writer.write(callback.as_sink(), plan);
  EXPECT_EQ(out.size(), plan.total_size);
  EXPECT_TRUE(stats.plan_mismatch);
}

TEST(ElfCoreWriter, TinyScratchIsRejected) {
  core_fixture fixture;
  const microfmt::address_space<fake_core_tag> space(fixture.memory);
  const cd::span_core_source source(fixture.regions, fixture.threads, fixture.files);
  const cd::elf_core_writer<cd::arch_x86_64, cd::span_core_source> writer(
      source, space.ref(), microfmt::span<char>(fixture.chunk.data(), 4));
  EXPECT_FALSE(writer.plan().valid);
}

uint8_t fake_regset_byte(uint32_t type, size_t offset) { return static_cast<uint8_t>((offset * 7U + type) & 0xFFU); }

// Register sets are fetched as whole blobs; the byte pattern lets tests check the exact placement.
bool fill_fake_regset(uint32_t index, void *out, size_t size) {
  if (index < cd::regset_index_base)
    return false;
  const microfmt::span<uint8_t> bytes(static_cast<uint8_t *>(out), size);
  for (size_t i = 0; i < size; ++i)
    bytes[i] = fake_regset_byte(index - cd::regset_index_base, i);
  return true;
}

bool read_fake_arm_register(const void *, microfmt::address_space_ref, uint32_t index, void *out,
                            size_t size) noexcept {
  if (index == cd::regset_index(cd::nt_arm_vfp) && size == 260)
    return fill_fake_regset(index, out, size);
  if (size == 4) {
    uint32_t value = 0;
    if (index == 15)
      value = 0x08001234;
    else if (index == 13)
      value = 0x20002000;
    else if (index == 264)
      value = 0x01000000;
    else
      return false;
    *static_cast<uint32_t *>(out) = value;
    return true;
  }
  if (size == 8 && index == 64) {
    *static_cast<uint64_t *>(out) = 0x3ff0000000000000ULL;
    return true;
  }
  return false;
}

template <typename Arch> std::vector<char> dump_arm(cd::core_plan &plan, cd::core_stats &stats) {
  fake_core_memory memory;
  const microfmt::address_space<fake_core_tag> space(memory);
  int unused_state = 0;
  std::byte register_scratch[8]{};
  cd::core_thread threads[] = {{7, 4,
                                microfmt::make_read_only_register_context_ref<read_fake_arm_register>(
                                    unused_state, space.ref(), register_scratch)}};
  cd::core_region regions[] = {{base_address, 2 * page, cd::pf_r}};
  const cd::span_core_source source(regions, threads);
  microfmt::array<char, 1024> chunk{};
  const cd::elf_core_writer<Arch, cd::span_core_source> writer(source, space.ref(), chunk);
  plan = writer.plan();
  std::vector<char> out;
  auto append = [&out](microfmt::string_view text) { out.insert(out.end(), text.begin(), text.end()); };
  microfmt::callback_sink<decltype(append)> callback(append);
  stats = writer.write(callback.as_sink(), plan);
  return out;
}

TEST(ElfCoreWriterArm, Elf32HeaderAndRegisters) {
  cd::core_plan plan;
  cd::core_stats stats;
  const auto out = dump_arm<cd::arch_arm_cortex_m>(plan, stats);
  EXPECT_EQ(out.size(), plan.total_size);
  EXPECT_FALSE(stats.plan_mismatch);
  EXPECT_EQ(load_le(out, 4, 1), 1U); // 32-bit
  EXPECT_EQ(load_le(out, 18, 2), 40U);
  EXPECT_EQ(load_le(out, 36, 4), 0x05000000U);
  EXPECT_EQ(load_le(out, 44, 2), 2U); // note + one load
  EXPECT_EQ(load_le(out, 52, 4), 4U); // PT_NOTE first
  const size_t note = load_le(out, 52 + 4, 4);
  EXPECT_EQ(load_le(out, note + 4, 4), 148U);
  const size_t body = note + 20;
  EXPECT_EQ(load_le(out, body, 4), 4U);
  EXPECT_EQ(load_le(out, body + 24, 4), 7U);
  EXPECT_EQ(load_le(out, body + 72 + 4 * 15, 4), 0x08001234U);
  EXPECT_EQ(load_le(out, body + 72 + 4 * 13, 4), 0x20002000U);
  EXPECT_EQ(load_le(out, body + 72 + 4 * 16, 4), 0x01000000U);
  EXPECT_EQ(load_le(out, 52 + 32 + 8, 4), base_address); // load vaddr
  EXPECT_EQ(plan.note_bytes, 20U + 148U);
}

TEST(ElfCoreWriterArm, VfpNoteIsAddedOnlyForVfpPolicy) {
  cd::core_plan plan;
  cd::core_stats stats;
  const auto out = dump_arm<cd::arch_arm_cortex_m_vfp>(plan, stats);
  EXPECT_EQ(out.size(), plan.total_size);
  EXPECT_FALSE(stats.plan_mismatch);
  EXPECT_EQ(plan.note_bytes, (20U + 148U) + (20U + 260U));
  const size_t vfp = load_le(out, 52 + 4, 4) + 20 + 148;
  EXPECT_EQ(load_le(out, vfp + 4, 4), 260U);
  EXPECT_EQ(load_le(out, vfp + 8, 4), 0x400U);
  EXPECT_EQ(load_le(out, vfp + 20, 1), fake_regset_byte(cd::nt_arm_vfp, 0));
  EXPECT_EQ(load_le(out, vfp + 20 + 259, 1), fake_regset_byte(cd::nt_arm_vfp, 259));
}

bool read_fake_wide_register(const void *, microfmt::address_space_ref, uint32_t index, void *out,
                             size_t size) noexcept {
  if (index >= cd::regset_index_base)
    return fill_fake_regset(index, out, size);
  if (index != 0 && index != 2 && index != 8 && index != 31 && index != 32 && index != 65 && index != 304)
    return false;
  if (size == 8)
    *static_cast<uint64_t *>(out) = 0xA0U + index;
  else if (size == 4)
    *static_cast<uint32_t *>(out) = 0xA0U + index;
  else
    return false;
  return true;
}

template <typename Arch> std::vector<char> dump_wide(cd::core_plan &plan) {
  fake_core_memory memory;
  const microfmt::address_space<fake_core_tag> space(memory);
  int unused_state = 0;
  std::byte register_scratch[8]{};
  cd::core_thread threads[] = {{7, 4,
                                microfmt::make_read_only_register_context_ref<read_fake_wide_register>(
                                    unused_state, space.ref(), register_scratch)}};
  cd::core_region regions[] = {{base_address, page, cd::pf_r}};
  const cd::span_core_source source(regions, threads);
  microfmt::array<char, 1024> chunk{};
  const cd::elf_core_writer<Arch, cd::span_core_source> writer(source, space.ref(), chunk);
  plan = writer.plan();
  std::vector<char> out;
  auto append = [&out](microfmt::string_view text) { out.insert(out.end(), text.begin(), text.end()); };
  microfmt::callback_sink<decltype(append)> callback(append);
  EXPECT_FALSE(writer.write(callback.as_sink(), plan).plan_mismatch);
  return out;
}

TEST(ElfCoreWriterArch, AArch64Registers) {
  cd::core_plan plan;
  const auto out = dump_wide<cd::arch_aarch64>(plan);
  EXPECT_EQ(out.size(), plan.total_size);
  EXPECT_EQ(load_le(out, 4, 1), 2U);
  EXPECT_EQ(load_le(out, 18, 2), 183U);
  const size_t note = load_le(out, 64 + 8, 8);
  EXPECT_EQ(load_le(out, note + 4, 4), 392U);
  const size_t regs = note + 20 + 112;
  EXPECT_EQ(load_le(out, regs, 8), 0xA0U);                 // x0
  EXPECT_EQ(load_le(out, regs + 8, 8), 0U);                // x1 unreadable
  EXPECT_EQ(load_le(out, regs + 8 * 31, 8), 0xA0U + 31U);  // sp
  EXPECT_EQ(load_le(out, regs + 8 * 32, 8), 0xA0U + 32U);  // pc
  EXPECT_EQ(load_le(out, regs + 8 * 33, 8), 0xA0U + 304U); // pstate
}

TEST(ElfCoreWriterArch, RiscvRegistersPutPcFirst) {
  cd::core_plan plan64;
  const auto rv64 = dump_wide<cd::arch_riscv64>(plan64);
  EXPECT_EQ(rv64.size(), plan64.total_size);
  EXPECT_EQ(load_le(rv64, 4, 1), 2U);
  EXPECT_EQ(load_le(rv64, 18, 2), 243U);
  const size_t note64 = load_le(rv64, 64 + 8, 8);
  EXPECT_EQ(load_le(rv64, note64 + 4, 4), 376U);
  const size_t regs64 = note64 + 20 + 112;
  EXPECT_EQ(load_le(rv64, regs64, 8), 0xA0U + 65U);     // pc
  EXPECT_EQ(load_le(rv64, regs64 + 8, 8), 0U);          // x1 unreadable
  EXPECT_EQ(load_le(rv64, regs64 + 16, 8), 0xA0U + 2U); // x2 (sp)

  cd::core_plan plan32;
  const auto rv32 = dump_wide<cd::arch_riscv32>(plan32);
  EXPECT_EQ(rv32.size(), plan32.total_size);
  EXPECT_EQ(load_le(rv32, 4, 1), 1U);
  EXPECT_EQ(load_le(rv32, 18, 2), 243U);
  const size_t note32 = load_le(rv32, 52 + 4, 4);
  EXPECT_EQ(load_le(rv32, note32 + 4, 4), 204U);
  EXPECT_EQ(load_le(rv32, note32 + 20 + 72, 4), 0xA0U + 65U);
  EXPECT_EQ(load_le(rv32, note32 + 20 + 72 + 8, 4), 0xA0U + 2U);
}

TEST(ElfCoreWriter, AuxvNoteIsAppendedWithTerminator) {
  core_fixture fixture;
  fixture.auxv = {{6, 4096}, {25, 0x7ffd0000}};
  cd::core_plan plan;
  cd::core_stats stats;
  const auto out = fixture.dump(plan, stats);
  EXPECT_EQ(plan.auxv_entries, 2U);
  EXPECT_EQ(out.size(), plan.total_size);
  EXPECT_FALSE(stats.plan_mismatch);

  const size_t notes = load_le(out, 64 + 8, 8);
  const size_t auxv = notes + plan.note_bytes - (20 + 16 * 3);
  EXPECT_EQ(load_le(out, auxv + 4, 4), 48U);
  EXPECT_EQ(load_le(out, auxv + 8, 4), 6U); // NT_AUXV
  EXPECT_EQ(load_le(out, auxv + 20, 8), 6U);
  EXPECT_EQ(load_le(out, auxv + 28, 8), 4096U);
  EXPECT_EQ(load_le(out, auxv + 36, 8), 25U);
  EXPECT_EQ(load_le(out, auxv + 44, 8), 0x7ffd0000U);
  EXPECT_EQ(load_le(out, auxv + 52, 8), 0U); // AT_NULL
  EXPECT_EQ(load_le(out, auxv + 60, 8), 0U);
}

TEST(ElfCoreWriter, ZeroPagesAreSkippedOnRequest) {
  core_fixture fixture;
  fixture.memory.zero_page = 1;
  cd::core_plan kept_plan;
  cd::core_stats stats;
  fixture.dump(kept_plan, stats);
  EXPECT_EQ(kept_plan.segment_bytes, 7 * page);
  EXPECT_EQ(kept_plan.load_segments, 2U);

  fixture.skip_zero_pages = true;
  cd::core_plan plan;
  const auto out = fixture.dump(plan, stats);
  EXPECT_EQ(plan.segment_bytes, 6 * page);
  EXPECT_EQ(plan.load_segments, 3U); // page 0, page 2, pages 4..7
  EXPECT_EQ(out.size(), plan.total_size);
  EXPECT_FALSE(stats.plan_mismatch);
  EXPECT_EQ(stats.read_failures, 0U);
}

TEST(ElfCoreWriterRegset, X86FpuFetchesFxsaveBlob) {
  cd::core_plan plan;
  const auto out = dump_wide<cd::arch_x86_64_fpu>(plan);
  EXPECT_EQ(out.size(), plan.total_size);
  EXPECT_EQ(plan.note_bytes, (20U + 336U) + (20U + 512U));
  const size_t notes = load_le(out, 64 + 8, 8);
  const size_t fpreg = notes + 20 + 336;
  EXPECT_EQ(load_le(out, fpreg + 4, 4), 512U);
  EXPECT_EQ(load_le(out, fpreg + 8, 4), 2U); // NT_PRFPREG
  EXPECT_EQ(load_le(out, fpreg + 12, 4), 0x45524f43U);
  EXPECT_EQ(load_le(out, fpreg + 20, 1), fake_regset_byte(cd::nt_prfpreg, 0));
  EXPECT_EQ(load_le(out, fpreg + 20 + 511, 1), fake_regset_byte(cd::nt_prfpreg, 511));
}

TEST(ElfCoreWriterRegset, X86_32HasTwoSetsAndI386Layout) {
  cd::core_plan plan;
  const auto out = dump_wide<cd::arch_x86_32_fpu>(plan);
  EXPECT_EQ(out.size(), plan.total_size);
  EXPECT_EQ(load_le(out, 4, 1), 1U);
  EXPECT_EQ(load_le(out, 18, 2), 3U);
  const size_t notes = load_le(out, 52 + 4, 4);
  EXPECT_EQ(load_le(out, notes + 4, 4), 144U);
  EXPECT_EQ(load_le(out, notes + 20 + 72 + 4 * 6, 4), 0xA0U);      // eax
  EXPECT_EQ(load_le(out, notes + 20 + 72 + 4 * 12, 4), 0xA0U + 8); // eip
  EXPECT_EQ(plan.note_bytes, (20U + 144U) + (20U + 108U) + (12U + 8U + 512U));
  const size_t fsave = notes + 20 + 144;
  EXPECT_EQ(load_le(out, fsave + 4, 4), 108U);
  EXPECT_EQ(load_le(out, fsave + 8, 4), 2U);
  const size_t fxsave = fsave + 20 + 108;
  EXPECT_EQ(load_le(out, fxsave + 4, 4), 512U);
  EXPECT_EQ(load_le(out, fxsave + 8, 4), 0x46e62b7fU);
  EXPECT_EQ(load_le(out, fxsave, 4), 6U);               // "LINUX\0"
  EXPECT_EQ(load_le(out, fxsave + 12, 4), 0x554e494cU); // "LINU"
}

TEST(ElfCoreWriterRegset, AArch64AndRiscvFpuSizes) {
  cd::core_plan plan;
  const auto a64 = dump_wide<cd::arch_aarch64_fpu>(plan);
  EXPECT_EQ(plan.note_bytes, (20U + 392U) + (20U + 528U));
  EXPECT_EQ(load_le(a64, load_le(a64, 64 + 8, 8) + 20 + 392 + 4, 4), 528U);

  const auto rv64 = dump_wide<cd::arch_riscv64_fpu>(plan);
  EXPECT_EQ(plan.note_bytes, (20U + 376U) + (20U + 264U));
  EXPECT_EQ(load_le(rv64, load_le(rv64, 64 + 8, 8) + 20 + 376 + 4, 4), 264U);

  const auto rv32 = dump_wide<cd::arch_riscv32_fpu>(plan);
  EXPECT_EQ(plan.note_bytes, (20U + 204U) + (20U + 264U));
  EXPECT_EQ(rv32.size(), plan.total_size);
}

TEST(ElfCoreWriterRegset, CortexAUsesCpsrAndFourKiloBytePages) {
  cd::core_plan plan;
  cd::core_stats stats;
  const auto out = dump_arm<cd::arch_arm_cortex_a>(plan, stats);
  EXPECT_EQ(out.size(), plan.total_size);
  EXPECT_FALSE(stats.plan_mismatch);
  EXPECT_EQ(load_le(out, 18, 2), 40U);
  EXPECT_EQ(plan.load_segments, 1U);
  // The fake reader serves 264 (xPSR), so a Cortex-A policy reading 259 (cpsr) stores zero there.
  const size_t body = load_le(out, 52 + 4, 4) + 20;
  EXPECT_EQ(load_le(out, body + 72 + 4 * 15, 4), 0x08001234U);
  EXPECT_EQ(load_le(out, body + 72 + 4 * 16, 4), 0U);
}

TEST(ElfCoreWriterRegset, UnreadableSetIsZeroFilledAndKeepsSize) {
  cd::core_plan plan;
  cd::core_stats stats;
  const auto out = dump_arm<cd::arch_x86_32_fpu>(plan, stats); // the ARM fake reader cannot serve x86 sets
  EXPECT_EQ(out.size(), plan.total_size);
  EXPECT_FALSE(stats.plan_mismatch);
  const size_t fxsave = load_le(out, 52 + 4, 4) + 20 + 144 + 20 + 108;
  EXPECT_EQ(load_le(out, fxsave + 8, 4), 0x46e62b7fU);
  EXPECT_EQ(load_le(out, fxsave + 20, 8), 0U);
}

TEST(ElfCoreWriterRegset, ScratchSmallerThanLargestSetIsRejected) {
  core_fixture fixture;
  const microfmt::address_space<fake_core_tag> space(fixture.memory);
  const cd::span_core_source source(fixture.regions, fixture.threads, fixture.files);
  const cd::elf_core_writer<cd::arch_x86_64_fpu, cd::span_core_source> writer(
      source, space.ref(), microfmt::span<char>(fixture.chunk.data(), 511));
  EXPECT_FALSE(writer.plan().valid);
}

template <typename Arch>
std::vector<char> dump_thread(const cd::core_thread &thread, microfmt::address_space_ref space, cd::core_plan &plan) {
  cd::core_thread threads[] = {thread};
  cd::core_region regions[] = {{base_address, page, cd::pf_r}};
  const cd::span_core_source source(regions, threads);
  microfmt::array<char, 1024> chunk{};
  const cd::elf_core_writer<Arch, cd::span_core_source> writer(source, space, chunk);
  plan = writer.plan();
  std::vector<char> out;
  auto append = [&out](microfmt::string_view text) { out.insert(out.end(), text.begin(), text.end()); };
  microfmt::callback_sink<decltype(append)> callback(append);
  EXPECT_FALSE(writer.write(callback.as_sink(), plan).plan_mismatch);
  return out;
}

TEST(CoredumpRegisterSets, X86_64StructFeedsRegistersAndFxsave) {
  fake_core_memory memory;
  const microfmt::address_space<fake_core_tag> space(memory);
  cd::x86_64_core_registers regs{};
  regs.gpr[cd::x86_64_core_registers::rip] = 0x401000;
  regs.gpr[cd::x86_64_core_registers::rsp] = 0x7fff0000;
  regs.eflags = 0x246;
  regs.segments[cd::x86_64_core_registers::cs] = 0x33;
  regs.fxsave[0] = 0xAB;
  regs.fxsave[511] = 0xCD;
  std::byte scratch[16]{};
  cd::x86_64_core_register_context mapped(regs, space.ref(), scratch);

  cd::core_plan plan;
  const auto out = dump_thread<cd::arch_x86_64_fpu>({7, 11, mapped.ref()}, space.ref(), plan);
  EXPECT_EQ(out.size(), plan.total_size);
  const size_t note = load_le(out, 64 + 8, 8);
  const size_t regs_at = note + 20 + 112;
  EXPECT_EQ(load_le(out, regs_at + 8 * 16, 8), 0x401000U);   // rip
  EXPECT_EQ(load_le(out, regs_at + 8 * 19, 8), 0x7fff0000U); // rsp
  EXPECT_EQ(load_le(out, regs_at + 8 * 18, 8), 0x246U);      // eflags
  EXPECT_EQ(load_le(out, regs_at + 8 * 17, 8), 0x33U);       // cs
  const size_t fpreg = note + 20 + 336 + 20;
  EXPECT_EQ(load_le(out, fpreg, 1), 0xABU);
  EXPECT_EQ(load_le(out, fpreg + 511, 1), 0xCDU);
}

TEST(CoredumpRegisterSets, CortexAndAarch64StructsFeedRegistersAndImages) {
  fake_core_memory memory;
  const microfmt::address_space<fake_core_tag> space(memory);
  std::byte scratch[16]{};

  cd::cortex_a_core_registers arm{};
  arm.r[15] = 0x08001234;
  arm.status = 0x60000010;
  arm.vfp[259] = 0x5A;
  cd::cortex_a_core_register_context arm_mapped(arm, space.ref(), scratch);
  cd::core_plan plan;
  const auto arm_out = dump_thread<cd::arch_arm_cortex_a_vfp>({7, 4, arm_mapped.ref()}, space.ref(), plan);
  EXPECT_EQ(arm_out.size(), plan.total_size);
  const size_t arm_note = load_le(arm_out, 52 + 4, 4);
  EXPECT_EQ(load_le(arm_out, arm_note + 20 + 72 + 4 * 15, 4), 0x08001234U);
  EXPECT_EQ(load_le(arm_out, arm_note + 20 + 72 + 4 * 16, 4), 0x60000010U);
  EXPECT_EQ(load_le(arm_out, arm_note + 20 + 148 + 20 + 259, 1), 0x5AU);

  cd::aarch64_core_registers a64{};
  a64.x[cd::aarch64_core_registers::pc] = 0xffff0000;
  a64.pstate = 0x3c5;
  a64.fpregs[527] = 0x77;
  cd::aarch64_core_register_context a64_mapped(a64, space.ref(), scratch);
  const auto a64_out = dump_thread<cd::arch_aarch64_fpu>({7, 4, a64_mapped.ref()}, space.ref(), plan);
  EXPECT_EQ(a64_out.size(), plan.total_size);
  const size_t a64_note = load_le(a64_out, 64 + 8, 8);
  EXPECT_EQ(load_le(a64_out, a64_note + 20 + 112 + 8 * 32, 8), 0xffff0000U);
  EXPECT_EQ(load_le(a64_out, a64_note + 20 + 112 + 8 * 33, 8), 0x3c5U);
  EXPECT_EQ(load_le(a64_out, a64_note + 20 + 392 + 20 + 527, 1), 0x77U);
}

struct parsed_frames {
  std::vector<char> payload;
  bool ok{false};
  uint32_t declared_total{0};
  size_t frames{0};
};

parsed_frames parse_frames(const std::vector<char> &stream) {
  parsed_frames result;
  size_t pos = 0;
  while (pos + 7 <= stream.size()) {
    if (load_le(stream, pos, 2) != 0x5aa5U)
      return result;
    const size_t type = load_le(stream, pos + 2, 1);
    const size_t length = load_le(stream, pos + 3, 2);
    if (pos + 5 + length + 2 > stream.size())
      return result;
    const std::string body(stream.begin() + static_cast<std::ptrdiff_t>(pos + 2),
                           stream.begin() + static_cast<std::ptrdiff_t>(pos + 5 + length));
    if (cd::detail::crc16_update(0xFFFF, body) != load_le(stream, pos + 5 + length, 2))
      return result;
    ++result.frames;
    if (type == cd::frame_type_data) {
      result.payload.insert(result.payload.end(), stream.begin() + static_cast<std::ptrdiff_t>(pos + 5),
                            stream.begin() + static_cast<std::ptrdiff_t>(pos + 5 + length));
    } else {
      result.declared_total = static_cast<uint32_t>(load_le(stream, pos + 5, 4));
      result.ok = pos + 5 + length + 2 == stream.size();
    }
    pos += 5 + length + 2;
  }
  return result;
}

TEST(CoredumpFramedSink, FramesRoundTripAndCarryTotal) {
  core_fixture fixture;
  cd::core_plan plan;
  cd::core_stats stats;
  const auto direct = fixture.dump(plan, stats);

  std::vector<char> wire;
  auto append = [&wire](microfmt::string_view text) { wire.insert(wire.end(), text.begin(), text.end()); };
  microfmt::callback_sink<decltype(append)> callback(append);
  cd::framed_sink<100> framed(callback.as_sink());
  const microfmt::address_space<fake_core_tag> space(fixture.memory);
  const cd::span_core_source source(fixture.regions, fixture.threads, fixture.files);
  const cd::elf_core_writer<cd::arch_x86_64, cd::span_core_source> writer(source, space.ref(), fixture.chunk);
  writer.write(framed.as_sink(), plan);
  framed.finish();

  const auto parsed = parse_frames(wire);
  EXPECT_TRUE(parsed.ok);
  EXPECT_EQ(parsed.payload, direct);
  EXPECT_EQ(parsed.declared_total, direct.size());
  EXPECT_EQ(parsed.frames, (direct.size() + 99) / 100 + 1);
}

TEST(CoredumpFramedSink, EmptyStreamStillHasEndFrame) {
  std::vector<char> wire;
  auto append = [&wire](microfmt::string_view text) { wire.insert(wire.end(), text.begin(), text.end()); };
  microfmt::callback_sink<decltype(append)> callback(append);
  cd::framed_sink<16> framed(callback.as_sink());
  framed.finish();
  const auto parsed = parse_frames(wire);
  EXPECT_TRUE(parsed.ok);
  EXPECT_TRUE(parsed.payload.empty());
  EXPECT_EQ(parsed.declared_total, 0U);
}

} // namespace
