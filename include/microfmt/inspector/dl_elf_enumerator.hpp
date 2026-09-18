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
 * bounds of the image whose load base was already resolved via `dladdr`.
 */
struct dl_find_size_state {
  uintptr_t target_base;
  uintptr_t size{0};
  bool found{false};
};

/**
 * @brief `dl_iterate_phdr` callback that records the mapped size of the
 * image whose base address matches @ref dl_find_size_state::target_base.
 */
inline int dl_find_size_callback(dl_phdr_info *info, size_t, void *data) noexcept {
  auto &state = *static_cast<dl_find_size_state *>(data);
  uintptr_t base = 0;
  uintptr_t size = 0;
  if (!dl_phdr_image_bounds(*info, base, size))
    return 0;
  if (base == state.target_base) {
    state.size = size;
    state.found = true;
    return 1; // Match found; stop iterating.
  }
  return 0;
}

/**
 * @brief Resolves the image owning @p pc via `dladdr`, then cross-references
 * `dl_iterate_phdr` to fill in the image's mapped size.
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
  if (size_state.found)
    result.image_size = size_state.size;

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
 * `exidx_start`/`exidx_end` and `debug_frame_start`/`debug_frame_end` are
 * always `0`: locating unwind-table sections requires parsing ELF section
 * headers, which neither `dladdr` nor `dl_iterate_phdr` expose.
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
