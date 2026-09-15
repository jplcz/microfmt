// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "../microfmt.hpp"
#include <cstdint>
#include <string_view>

namespace microfmt {

// ============================================================================
// ELF Image Record with EXIDX and Module Boundaries
// ============================================================================

struct elf_image_info {
  std::string_view image_name{""};
  uintptr_t load_base{0};
  uintptr_t image_size{0};

  // EXIDX section bounds
  uintptr_t exidx_start{0};
  uintptr_t exidx_end{0};

  // DWARF debug_frame section bounds
  uintptr_t debug_frame_start{0};
  uintptr_t debug_frame_end{0};

  [[nodiscard]] constexpr bool contains(uintptr_t addr) const noexcept {
    return addr >= load_base && addr < (load_base + image_size);
  }

  [[nodiscard]] constexpr bool has_exidx() const noexcept {
    return exidx_start != 0 && exidx_end > exidx_start;
  }

  [[nodiscard]] constexpr bool has_debug_frame() const noexcept {
    return debug_frame_start != 0 && debug_frame_end > debug_frame_start;
  }
};

// ============================================================================
// Type-Erased ELF Image Enumerator Handle
// ============================================================================

class elf_image_enumerator_ref {
public:
  struct vtable {
    bool (*enumerate)(const void *ctx, span<elf_image_info> out_buffer,
                      size_t &out_count) noexcept;
    bool (*find_by_pc)(const void *ctx, uintptr_t pc,
                       elf_image_info &out_info) noexcept;
  };

  constexpr elf_image_enumerator_ref() noexcept = default;

  template <typename Tag, typename Context>
  constexpr elf_image_enumerator_ref(Tag, const Context &ctx) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag, Context>) {}

  template <typename Fn>
  constexpr explicit elf_image_enumerator_ref(const Fn &fn) noexcept
      : ctx_(&fn), vtbl_(&s_fn_vtbl<Fn>) {}

  // Fills caller-provided buffer with registered ELF images
  [[nodiscard]] bool enumerate(span<elf_image_info> out_buffer,
                               size_t &out_count) const noexcept {
    if (!vtbl_)
      return false;
    return vtbl_->enumerate(ctx_, out_buffer, out_count);
  }

  // Locates the specific ELF image containing the given PC address
  [[nodiscard]] bool find_by_pc(uintptr_t pc,
                                elf_image_info &out_info) const noexcept {
    if (!vtbl_)
      return false;
    return vtbl_->find_by_pc(ctx_, pc, out_info);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return vtbl_ != nullptr;
  }

private:
  template <typename Tag, typename Context>
  static constexpr vtable s_vtbl{
      [](const void *c, span<elf_image_info> buf, size_t &count) noexcept {
        return static_cast<const Context *>(c)->enumerate(buf, count);
      },
      [](const void *c, uintptr_t pc, elf_image_info &info) noexcept {
        return static_cast<const Context *>(c)->find_by_pc(pc, info);
      }};

  template <typename Fn>
  static constexpr vtable s_fn_vtbl{
      [](const void *c, span<elf_image_info> buf, size_t &count) noexcept {
        return (*static_cast<const Fn *>(c)).enumerate(buf, count);
      },
      [](const void *c, uintptr_t pc, elf_image_info &info) noexcept {
        return (*static_cast<const Fn *>(c)).find_by_pc(pc, info);
      }};

  const void *ctx_{nullptr};
  const vtable *vtbl_{nullptr};
};

// ============================================================================
// Concrete Fixed-Capacity Multi-ELF Registry Context (Zero Allocation)
// ============================================================================

template <size_t MaxImages = 16> class multi_elf_registry_context {
public:
  constexpr multi_elf_registry_context() noexcept = default;

  constexpr bool register_image(const elf_image_info &info) noexcept {
    if (image_count_ >= MaxImages)
      return false;
    images_[image_count_++] = info;
    return true;
  }

  [[nodiscard]] constexpr bool enumerate(span<elf_image_info> out_buffer,
                                         size_t &out_count) const noexcept {
    size_t copy_count =
        (image_count_ < out_buffer.size()) ? image_count_ : out_buffer.size();
    for (size_t i = 0; i < copy_count; ++i) {
      out_buffer[i] = images_[i];
    }
    out_count = copy_count;
    return true;
  }

  [[nodiscard]] constexpr bool
  find_by_pc(uintptr_t pc, elf_image_info &out_info) const noexcept {
    for (size_t i = 0; i < image_count_; ++i) {
      if (images_[i].contains(pc)) {
        out_info = images_[i];
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] constexpr size_t size() const noexcept { return image_count_; }

private:
  elf_image_info images_[MaxImages]{};
  size_t image_count_{0};
};

struct multi_elf_registry_tag {};

} // namespace microfmt