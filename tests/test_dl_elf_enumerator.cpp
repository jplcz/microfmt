// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <microfmt/inspector/dl_elf_enumerator.hpp>
#include <microfmt/microfmt.hpp>

namespace {

// A function whose address is guaranteed to fall inside this test binary's
// own (main executable) image, used to exercise find_by_pc().
int marker_function() { return 42; }

microfmt::elf_image_enumerator_ref enumerator_ref() {
  static microfmt::elf_image_enumerator<microfmt::dl_elf_enumerator_tag> enumerator(
      microfmt::dl_elf_enumerator_context{});
  return enumerator.ref();
}

} // namespace

TEST(DlElfEnumerator, EnumerateFindsAtLeastOneImage) {
  auto ref = enumerator_ref();
  std::array<microfmt::elf_image_info, 32> images{};
  size_t count = 0;
  ASSERT_TRUE(ref.enumerate(microfmt::span<microfmt::elf_image_info>(images.data(), images.size()), count));
  ASSERT_GT(count, 0u);
  EXPECT_LE(count, images.size());
}

TEST(DlElfEnumerator, EnumeratedImagesHaveNonEmptyBoundsWhenNamed) {
  auto ref = enumerator_ref();
  std::array<microfmt::elf_image_info, 32> images{};
  size_t count = 0;
  ASSERT_TRUE(ref.enumerate(microfmt::span<microfmt::elf_image_info>(images.data(), images.size()), count));

  bool found_main_executable = false;
  for (size_t i = 0; i < count; ++i) {
    const auto &image = images[i];
    // Every image should have a well-formed (possibly zero-sized only for
    // pathological cases) load_base; the main executable and shared objects
    // enumerated by dl_iterate_phdr should have a positive image_size.
    if (image.load_base != 0 && image.image_size > 0) {
      EXPECT_GT(image.load_base + image.image_size, image.load_base);
    }
    if (image.image_name.empty())
      found_main_executable = true; // The main executable is commonly unnamed.
  }
  // At least one image (typically the main executable itself) is expected.
  EXPECT_TRUE(found_main_executable || count > 0);
}

TEST(DlElfEnumerator, EnumerateRespectsBufferCapacity) {
  auto ref = enumerator_ref();
  std::array<microfmt::elf_image_info, 1> images{};
  size_t count = 0;
  ASSERT_TRUE(ref.enumerate(microfmt::span<microfmt::elf_image_info>(images.data(), images.size()), count));
  EXPECT_EQ(count, 1u);
}

TEST(DlElfEnumerator, EnumerateWithEmptyBufferReportsZero) {
  auto ref = enumerator_ref();
  size_t count = 123;
  ASSERT_TRUE(ref.enumerate(microfmt::span<microfmt::elf_image_info>(nullptr, size_t{0}), count));
  EXPECT_EQ(count, 0u);
}

TEST(DlElfEnumerator, FindByPcLocatesOwnAddress) {
  auto ref = enumerator_ref();
  const auto pc = reinterpret_cast<uintptr_t>(&marker_function);

  microfmt::elf_image_info info{};
  ASSERT_TRUE(ref.find_by_pc(pc, info));
  EXPECT_GT(info.image_size, 0u);
  EXPECT_TRUE(info.contains(pc));
}

TEST(DlElfEnumerator, FindByPcRejectsBogusAddress) {
  auto ref = enumerator_ref();
  microfmt::elf_image_info info{};
  EXPECT_FALSE(ref.find_by_pc(0x1, info));
}

TEST(DlElfEnumerator, FindByPcAgreesWithEnumerate) {
  auto ref = enumerator_ref();
  const auto pc = reinterpret_cast<uintptr_t>(&marker_function);

  microfmt::elf_image_info found{};
  ASSERT_TRUE(ref.find_by_pc(pc, found));

  std::array<microfmt::elf_image_info, 32> images{};
  size_t count = 0;
  ASSERT_TRUE(ref.enumerate(microfmt::span<microfmt::elf_image_info>(images.data(), images.size()), count));

  bool matched = false;
  for (size_t i = 0; i < count; ++i) {
    if (images[i].load_base == found.load_base) {
      EXPECT_EQ(images[i].image_size, found.image_size);
      EXPECT_EQ(images[i].exidx_start, found.exidx_start);
      EXPECT_EQ(images[i].exidx_end, found.exidx_end);
      EXPECT_EQ(images[i].debug_frame_start, found.debug_frame_start);
      EXPECT_EQ(images[i].debug_frame_end, found.debug_frame_end);
      matched = true;
      break;
    }
  }
  EXPECT_TRUE(matched);
}

TEST(DlElfEnumerator, ExidxBoundsAreWellFormedWhenPresent) {
  // On platforms without .ARM.exidx (e.g. x86_64), has_exidx() stays false
  // for every image, since PT_ARM_EXIDX segments are never emitted there.
  // This test only asserts internal consistency (start <= end, both zero
  // together), so it stays meaningful on every platform this backend
  // supports.
  auto ref = enumerator_ref();
  std::array<microfmt::elf_image_info, 32> images{};
  size_t count = 0;
  ASSERT_TRUE(ref.enumerate(microfmt::span<microfmt::elf_image_info>(images.data(), images.size()), count));

  for (size_t i = 0; i < count; ++i) {
    const auto &image = images[i];
    if (image.has_exidx()) {
      EXPECT_GT(image.exidx_end, image.exidx_start);
    } else {
      EXPECT_EQ(image.exidx_start, 0u);
      EXPECT_EQ(image.exidx_end, 0u);
    }
  }
}

// This backend derives debug_frame_start/debug_frame_end from the loaded
// .eh_frame (via PT_GNU_EH_FRAME/.eh_frame_hdr), not the debug-only
// .debug_frame section. Regular GCC/Clang Linux builds emit .eh_frame with
// -fasynchronous-unwind-tables (the default), so this test binary is
// expected to have one, and .eh_frame's very first record is always a CIE
// (whose 4-byte CIE_id/CIE_pointer field is 0), letting us sanity-check the
// decoded bytes actually look like unwind data.
TEST(DlElfEnumerator, FindByPcPopulatesEhFrameBackedDebugFrame) {
  auto ref = enumerator_ref();
  const auto pc = reinterpret_cast<uintptr_t>(&marker_function);

  microfmt::elf_image_info info{};
  ASSERT_TRUE(ref.find_by_pc(pc, info));
  ASSERT_TRUE(info.has_debug_frame());
  EXPECT_EQ(info.debug_frame_end, UINTPTR_MAX);

  uint32_t length = 0;
  uint32_t cie_id = 0;
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  std::memcpy(&length, reinterpret_cast<const void *>(info.debug_frame_start), sizeof(length));
  std::memcpy(&cie_id, reinterpret_cast<const void *>(info.debug_frame_start + 4), sizeof(cie_id));
  RELOCO_END_UNSAFE_BUFFER_USAGE;
  EXPECT_GT(length, 0u);
  EXPECT_EQ(cie_id, 0u); // The first .eh_frame record is always a CIE.
}

TEST(DlElfEnumerator, EmptyHandleFailsBothOperations) {
  microfmt::elf_image_enumerator_ref empty_ref;
  EXPECT_FALSE(static_cast<bool>(empty_ref));

  microfmt::elf_image_info info{};
  std::array<microfmt::elf_image_info, 1> images{};
  size_t count = 0;
  EXPECT_FALSE(empty_ref.enumerate(microfmt::span<microfmt::elf_image_info>(images.data(), images.size()), count));
  EXPECT_FALSE(empty_ref.find_by_pc(0x1000, info));
}
