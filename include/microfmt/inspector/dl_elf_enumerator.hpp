// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file dl_elf_enumerator.hpp @brief `dlfcn.h`/`dl_iterate_phdr`-backed
 * @ref microfmt::elf_image_enumerator_ref implementation for Linux and BSD. */

#pragma once

#if !defined(__linux__) && !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__NetBSD__) &&                 \
    !defined(__DragonFly__)
#error "microfmt/inspector/dl_elf_enumerator.hpp only supports Linux and BSD systems"
#endif

#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <link.h>

#include "../microfmt.hpp"
#include "elf_enumerator.hpp"

namespace microfmt::detail {

/**
 * @brief Computes the runtime `[base, base + size)` range covered by an
 * image's `PT_LOAD` segments.
 * @param info Program-header info supplied by `dl_iterate_phdr`.
 * @param out_base Receives the lowest mapped address.
 * @param out_size Receives the total mapped size.
 * @return `true` when at least one `PT_LOAD` segment was found.
 */
inline bool dl_phdr_image_bounds(const dl_phdr_info &info, uintptr_t &out_base, uintptr_t &out_size) noexcept {
  uintptr_t min_vaddr = 0;
  uintptr_t max_vaddr = 0;
  bool have_load = false;

  MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

  for (size_t i = 0; i < static_cast<size_t>(info.dlpi_phnum); ++i) {
    const auto &phdr = info.dlpi_phdr[i];
    if (phdr.p_type != PT_LOAD)
      continue;
    const auto start = static_cast<uintptr_t>(phdr.p_vaddr);
    const auto end = start + static_cast<uintptr_t>(phdr.p_memsz);
    if (!have_load || start < min_vaddr)
      min_vaddr = start;
    if (!have_load || end > max_vaddr)
      max_vaddr = end;
    have_load = true;
  }

  MICROFMT_END_UNSAFE_BUFFER_USAGE;

  if (!have_load)
    return false;
  out_base = static_cast<uintptr_t>(info.dlpi_addr) + min_vaddr;
  out_size = max_vaddr - min_vaddr;
  return true;
}

#if defined(PT_ARM_EXIDX)
/**
 * @brief Locates an image's `PT_ARM_EXIDX` segment (the runtime-mapped
 * `.ARM.exidx` unwind table), if any.
 * @param info Program-header info supplied by `dl_iterate_phdr`.
 * @param out_start Receives the start address of the `.ARM.exidx` table.
 * @param out_end Receives the end address of the `.ARM.exidx` table.
 * @return `true` when a non-empty `PT_ARM_EXIDX` segment was found.
 */
inline bool dl_phdr_exidx_bounds(const dl_phdr_info &info, uintptr_t &out_start, uintptr_t &out_end) noexcept {
  uintptr_t start = 0;
  uintptr_t end = 0;
  bool have_exidx = false;

  MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

  for (size_t i = 0; i < static_cast<size_t>(info.dlpi_phnum); ++i) {
    const auto &phdr = info.dlpi_phdr[i];
    if (phdr.p_type != PT_ARM_EXIDX)
      continue;
    start = static_cast<uintptr_t>(info.dlpi_addr) + static_cast<uintptr_t>(phdr.p_vaddr);
    end = start + static_cast<uintptr_t>(phdr.p_memsz);
    have_exidx = true;
    break;
  }

  MICROFMT_END_UNSAFE_BUFFER_USAGE;

  if (!have_exidx || end <= start)
    return false;
  out_start = start;
  out_end = end;
  return true;
}
#endif // defined(PT_ARM_EXIDX)

#if defined(PT_GNU_EH_FRAME)

// Subset of the DW_EH_PE_* pointer-encoding constants (from the LSB/libgcc
// `.eh_frame_hdr` ABI) needed to decode the `eh_frame_ptr` field. Only the
// application/format combinations GCC and Clang actually emit are handled.
inline constexpr unsigned char k_dw_eh_pe_omit = 0xff;
inline constexpr unsigned char k_dw_eh_pe_format_mask = 0x0f;
inline constexpr unsigned char k_dw_eh_pe_application_mask = 0x70;
inline constexpr unsigned char k_dw_eh_pe_udata4 = 0x03;
inline constexpr unsigned char k_dw_eh_pe_sdata4 = 0x0b;
inline constexpr unsigned char k_dw_eh_pe_pcrel = 0x10;
inline constexpr unsigned char k_dw_eh_pe_absptr_application = 0x00;

/**
 * @brief Decodes a `PT_GNU_EH_FRAME`-mapped `.eh_frame_hdr` header to recover
 * the runtime start address of the corresponding `.eh_frame` section, using
 * the same layout libgcc's unwinder (`_Unwind_Find_FDE`) consults: a 4-byte
 * fixed header (`version`, `eh_frame_ptr_enc`, `fde_count_enc`,
 * `table_enc`), immediately followed by the encoded `eh_frame_ptr` value.
 * @param hdr_start Runtime start address of the `.eh_frame_hdr` segment.
 * @param hdr_end Runtime end address of the `.eh_frame_hdr` segment.
 * @param out_eh_frame_start Receives the decoded `.eh_frame` start address.
 * @return `true` when the header used a supported encoding and was decoded.
 */
inline bool decode_eh_frame_hdr(uintptr_t hdr_start, uintptr_t hdr_end, uintptr_t &out_eh_frame_start) noexcept {
  // 4-byte fixed header + at least a 4-byte encoded eh_frame_ptr field.
  if (hdr_end < hdr_start || hdr_end - hdr_start < 8)
    return false;

  MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  const auto *bytes = reinterpret_cast<const unsigned char *>(hdr_start);
  const unsigned char version = bytes[0];
  const unsigned char eh_frame_ptr_enc = bytes[1];
  MICROFMT_END_UNSAFE_BUFFER_USAGE;

  if (version != 1 || eh_frame_ptr_enc == k_dw_eh_pe_omit)
    return false;

  const unsigned char format = eh_frame_ptr_enc & k_dw_eh_pe_format_mask;
  const unsigned char application = eh_frame_ptr_enc & k_dw_eh_pe_application_mask;
  if (application != k_dw_eh_pe_pcrel && application != k_dw_eh_pe_absptr_application)
    return false; // Uncommon application; not worth mis-decoding.
  if (format != k_dw_eh_pe_udata4 && format != k_dw_eh_pe_sdata4)
    return false; // Uncommon format; not worth mis-decoding.

  const uintptr_t field_addr = hdr_start + 4;
  int32_t raw_value = 0;
  MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  std::memcpy(&raw_value, reinterpret_cast<const void *>(field_addr), sizeof(raw_value));
  MICROFMT_END_UNSAFE_BUFFER_USAGE;

  const uintptr_t value = (format == k_dw_eh_pe_udata4) ? static_cast<uintptr_t>(static_cast<uint32_t>(raw_value))
                                                         : static_cast<uintptr_t>(raw_value);

  out_eh_frame_start = (application == k_dw_eh_pe_pcrel) ? field_addr + value : value;
  return true;
}

/**
 * @brief Locates an image's `PT_GNU_EH_FRAME` segment (the `.eh_frame_hdr`),
 * decodes it to recover the runtime `.eh_frame` start address, and reports
 * it as the record's `debug_frame_start`/`debug_frame_end` fields.
 * `.eh_frame` is the loaded, CIE/FDE-encoded unwind table GCC/Clang emit by
 * default (unlike `.debug_frame`, which is debug-only and typically not
 * mapped at runtime), and its records are decodable by the same DWARF CFI
 * decoder either field is meant to feed.
 *
 * `.eh_frame_hdr` records where `.eh_frame` starts but not its size.
 * Following LLVM libunwind's `checkForUnwindInfoSegment`, `out_end` is set
 * to `UINTPTR_MAX`; callers are expected to stop at `.eh_frame`'s standard
 * zero-length terminator (or the first unreadable address) rather than at
 * an explicit end bound, exactly as `dwarf_decoder.hpp`'s FDE walk already
 * does.
 * @param info Program-header info supplied by `dl_iterate_phdr`.
 * @param out_start Receives the start address of the `.eh_frame` table.
 * @param out_end Receives `UINTPTR_MAX` (see above).
 * @return `true` when a `PT_GNU_EH_FRAME` segment was found and decoded.
 */
inline bool dl_phdr_eh_frame_bounds(const dl_phdr_info &info, uintptr_t &out_start, uintptr_t &out_end) noexcept {
  uintptr_t hdr_start = 0;
  uintptr_t hdr_end = 0;
  bool have_eh_frame_hdr = false;

  MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

  for (size_t i = 0; i < static_cast<size_t>(info.dlpi_phnum); ++i) {
    const auto &phdr = info.dlpi_phdr[i];
    if (phdr.p_type != PT_GNU_EH_FRAME)
      continue;
    hdr_start = static_cast<uintptr_t>(info.dlpi_addr) + static_cast<uintptr_t>(phdr.p_vaddr);
    hdr_end = hdr_start + static_cast<uintptr_t>(phdr.p_memsz);
    have_eh_frame_hdr = true;
    break;
  }

  MICROFMT_END_UNSAFE_BUFFER_USAGE;

  if (!have_eh_frame_hdr)
    return false;

  uintptr_t eh_frame_start = 0;
  if (!decode_eh_frame_hdr(hdr_start, hdr_end, eh_frame_start))
    return false;

  out_start = eh_frame_start;
  out_end = UINTPTR_MAX;
  return true;
}
#endif // defined(PT_GNU_EH_FRAME)

/**
 * @brief Mutable state threaded through @ref dl_enumerate_callback.
 */
struct dl_enumerate_state {
  span<elf_image_info> buffer;
  size_t count{0};
};

/**
 * @brief `dl_iterate_phdr` callback that fills @ref dl_enumerate_state::buffer
 * with one @ref elf_image_info per loaded image, stopping once full.
 */
inline int dl_enumerate_callback(dl_phdr_info *info, size_t, void *data) noexcept {
  auto &state = *static_cast<dl_enumerate_state *>(data);
  if (state.count >= state.buffer.size())
    return 1; // Buffer already full; stop iterating.

  uintptr_t base = 0;
  uintptr_t size = 0;
  if (!dl_phdr_image_bounds(*info, base, size))
    return 0; // No PT_LOAD segments (e.g. the Linux VDSO on some ABIs); skip.

  elf_image_info entry{};
  entry.image_name =
      (info->dlpi_name && info->dlpi_name[0] != '\0') ? string_view(info->dlpi_name) : string_view("");
  entry.load_base = base;
  entry.image_size = size;
#if defined(PT_ARM_EXIDX)
  dl_phdr_exidx_bounds(*info, entry.exidx_start, entry.exidx_end);
#endif
#if defined(PT_GNU_EH_FRAME)
  dl_phdr_eh_frame_bounds(*info, entry.debug_frame_start, entry.debug_frame_end);
#endif
  state.buffer[state.count++] = entry;
  return 0;
}

/**
 * @brief Fills @p out_buffer with every image currently reported by
 * `dl_iterate_phdr`, up to its capacity.
 * @param out_buffer Destination buffer for image records.
 * @param out_count Receives the number of images written (`<= out_buffer.size()`).
 * @return Always `true`.
 */
inline bool dl_enumerate_images(span<elf_image_info> out_buffer, size_t &out_count) noexcept {
  dl_enumerate_state state{out_buffer, 0};
  ::dl_iterate_phdr(&dl_enumerate_callback, &state);
  out_count = state.count;
  return true;
}

/**
 * @brief State used by @ref dl_find_size_callback to locate the `PT_LOAD`
 * (and, where available, `PT_ARM_EXIDX`/`PT_GNU_EH_FRAME`) bounds of the
 * image whose load base was already resolved via `dladdr`.
 */
struct dl_find_size_state {
  uintptr_t target_base;
  uintptr_t size{0};
  uintptr_t exidx_start{0};
  uintptr_t exidx_end{0};
  uintptr_t debug_frame_start{0};
  uintptr_t debug_frame_end{0};
  bool found{false};
};

/**
 * @brief `dl_iterate_phdr` callback that records the mapped size (and, where
 * available, the `.ARM.exidx`/`.eh_frame` bounds) of the image whose base
 * address matches @ref dl_find_size_state::target_base.
 */
inline int dl_find_size_callback(dl_phdr_info *info, size_t, void *data) noexcept {
  auto &state = *static_cast<dl_find_size_state *>(data);
  uintptr_t base = 0;
  uintptr_t size = 0;
  if (!dl_phdr_image_bounds(*info, base, size))
    return 0;
  if (base == state.target_base) {
    state.size = size;
#if defined(PT_ARM_EXIDX)
    dl_phdr_exidx_bounds(*info, state.exidx_start, state.exidx_end);
#endif
#if defined(PT_GNU_EH_FRAME)
    dl_phdr_eh_frame_bounds(*info, state.debug_frame_start, state.debug_frame_end);
#endif
    state.found = true;
    return 1; // Match found; stop iterating.
  }
  return 0;
}

/**
 * @brief Resolves the image owning @p pc via `dladdr`, then cross-references
 * `dl_iterate_phdr` to fill in the image's mapped size and, where available,
 * its `.ARM.exidx`/`.eh_frame` bounds.
 * @param pc Code address to look up.
 * @param out_info Receives the owning image record.
 * @return `true` when `dladdr` located an owning image.
 */
inline bool dl_find_by_pc(uintptr_t pc, elf_image_info &out_info) noexcept {
  Dl_info info{};
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  if (::dladdr(reinterpret_cast<const void *>(pc), &info) == 0 || info.dli_fbase == nullptr)
    return false;

  elf_image_info result{};
  result.image_name = (info.dli_fname && info.dli_fname[0] != '\0') ? string_view(info.dli_fname) : string_view("");
  result.load_base = reinterpret_cast<uintptr_t>(info.dli_fbase);

  dl_find_size_state size_state{result.load_base};
  ::dl_iterate_phdr(&dl_find_size_callback, &size_state);
  if (size_state.found) {
    result.image_size = size_state.size;
    result.exidx_start = size_state.exidx_start;
    result.exidx_end = size_state.exidx_end;
    result.debug_frame_start = size_state.debug_frame_start;
    result.debug_frame_end = size_state.debug_frame_end;
  }

  out_info = result;
  return true;
}

} // namespace microfmt::detail

namespace microfmt {

/**
 * @brief Tag selecting the `dlfcn.h`/`dl_iterate_phdr`-backed
 * @ref elf_image_enumerator_traits specialization.
 */
struct dl_elf_enumerator_tag {};

/**
 * @brief Empty context for @ref dl_elf_enumerator_tag: `dladdr` and
 * `dl_iterate_phdr` query the dynamic linker directly and require no
 * per-instance state.
 */
struct dl_elf_enumerator_context {};

/**
 * @brief Traits binding @ref dl_elf_enumerator_tag to `dladdr`/
 * `dl_iterate_phdr`-based ELF image introspection.
 *
 * `image_name` in returned records points to storage owned by the dynamic
 * linker (typically the link map's copy of the path used to load the
 * image) and remains valid for as long as that image stays loaded.
 * `exidx_start`/`exidx_end` are populated from the image's `PT_ARM_EXIDX`
 * program header when the platform defines `PT_ARM_EXIDX` and the image has
 * one (e.g. 32-bit ARM/AArch32 binaries built with `.ARM.exidx` unwind
 * tables); they are `0` otherwise. `debug_frame_start`/`debug_frame_end` are
 * populated, when the platform defines `PT_GNU_EH_FRAME` and the image has
 * one, from the loaded `.eh_frame` unwind table located via that segment's
 * `.eh_frame_hdr` (the same technique LLVM libunwind's
 * `checkForUnwindInfoSegment` and libgcc's `_Unwind_Find_FDE` use) — *not*
 * from the DWARF `.debug_frame` section itself, which is debug-only,
 * unloaded at runtime, and has no program-header entry to locate it by.
 * `debug_frame_end` is set to `UINTPTR_MAX` in that case, since
 * `.eh_frame_hdr` records where `.eh_frame` starts but not its size; callers
 * (e.g. `dwarf_decoder.hpp`) are expected to stop at `.eh_frame`'s standard
 * zero-length terminator or the first unreadable address. Both fields stay
 * `0` when the platform lacks `PT_GNU_EH_FRAME` or the image has no such
 * segment.
 */
template <> struct elf_image_enumerator_traits<dl_elf_enumerator_tag> {
  using context_type = dl_elf_enumerator_context;

  static bool enumerate(value_ref<const context_type>, span<elf_image_info> buffer, size_t &count) noexcept {
    return detail::dl_enumerate_images(buffer, count);
  }

  static bool find_by_pc(value_ref<const context_type>, uintptr_t pc, elf_image_info &out_info) noexcept {
    return detail::dl_find_by_pc(pc, out_info);
  }
};

} // namespace microfmt
