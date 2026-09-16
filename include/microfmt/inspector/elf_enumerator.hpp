// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file elf_enumerator.hpp @brief ELF image introspection, type-erased image
 * enumeration, and fixed-capacity registries. */

#include "../microfmt.hpp"
#include <cstdint>
#include <string_view>

namespace microfmt {

// ============================================================================
// ELF Image Record with EXIDX and Module Boundaries
// ============================================================================

/**
 * @brief Describes a loaded ELF image and the locations of its unwind tables.
 */
struct elf_image_info {
  /**
   * @brief Human-readable image/module name.
   */
  microfmt::string_view image_name{""};
  /**
   * @brief Load (base) address of the image in the target address space.
   */
  uintptr_t load_base{0};
  /**
   * @brief Total size of the image in bytes.
   */
  uintptr_t image_size{0};

  /**
   * @brief Start of the `.ARM.exidx` section.
   */
  uintptr_t exidx_start{0};
  /**
   * @brief End of the `.ARM.exidx` section.
   */
  uintptr_t exidx_end{0};

  /**
   * @brief Start of the DWARF `.debug_frame` section.
   */
  uintptr_t debug_frame_start{0};
  /**
   * @brief End of the DWARF `.debug_frame` section.
   */
  uintptr_t debug_frame_end{0};

  /**
   * @brief Reports whether @p addr falls within the image range.
   * @param addr Address to test.
   * @return `true` when `load_base <= addr < load_base + image_size`.
   */
  [[nodiscard]] constexpr bool contains(uintptr_t addr) const noexcept {
    return addr >= load_base && addr < (load_base + image_size);
  }

  /**
   * @brief Reports whether a usable `.ARM.exidx` section is present.
   * @return `true` when the section bounds are valid.
   */
  [[nodiscard]] constexpr bool has_exidx() const noexcept { return exidx_start != 0 && exidx_end > exidx_start; }

  /**
   * @brief Reports whether a usable DWARF `.debug_frame` section is present.
   * @return `true` when the section bounds are valid.
   */
  [[nodiscard]] constexpr bool has_debug_frame() const noexcept {
    return debug_frame_start != 0 && debug_frame_end > debug_frame_start;
  }
};

// ============================================================================
// Type-Erased ELF Image Enumerator Handle
// ============================================================================

/**
 * @brief Type-erased, two-word handle to an ELF image enumerator.
 *
 * Supports both tag/context-backed implementations and simple functor
 * adapters, with zero allocation and no virtual dispatch.
 */
class MICROFMT_POINTER elf_image_enumerator_ref {
public:
  /**
   * @brief Virtual table of image enumeration operations.
   */
  struct vtable {
    /**
     * @brief Fills a caller buffer with loaded images. See
     * @ref elf_image_enumerator_ref::enumerate.
     */
    bool (*enumerate)(const void *ctx, span<elf_image_info> out_buffer, size_t &out_count) noexcept;
    /**
     * @brief Locates the image owning a PC. See
     * @ref elf_image_enumerator_ref::find_by_pc.
     */
    bool (*find_by_pc)(const void *ctx, uintptr_t pc, elf_image_info &out_info) noexcept;
  };

  /**
   * @brief Constructs an empty (invalid) handle.
   */
  constexpr elf_image_enumerator_ref() noexcept = default;

  /**
   * @brief Constructs a handle from a tag and its context object.
   * @tparam Tag Implementation tag type.
   * @tparam Context Concrete context type exposing `enumerate` and
   * `find_by_pc`.
   * @param ctx Context object performing the enumeration.
   */
  template <typename Tag, typename Context>
  constexpr elf_image_enumerator_ref(Tag, const Context &ctx MICROFMT_LIFETIMEBOUND) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag, Context>) {}

  /**
   * @brief Constructs a handle from a callable functor.
   * @tparam Fn Functor type exposing `enumerate` and `find_by_pc` members.
   * @param fn Functor performing the enumeration.
   */
  template <typename Fn>
  constexpr explicit elf_image_enumerator_ref(const Fn &fn MICROFMT_LIFETIMEBOUND) noexcept
      : ctx_(&fn), vtbl_(&s_fn_vtbl<Fn>) {}

  /**
   * @brief Fills a caller-provided buffer with registered ELF images.
   * @param out_buffer Destination buffer for image records.
   * @param out_count Receives the number of images stored.
   * @return `true` on success, `false` if the handle is empty.
   */
  [[nodiscard]] bool enumerate(span<elf_image_info> out_buffer, size_t &out_count) const noexcept {
    if (!vtbl_)
      return false;
    return vtbl_->enumerate(ctx_.get(), out_buffer, out_count);
  }

  /**
   * @brief Locates the ELF image containing the given PC address.
   * @param pc Code address to look up.
   * @param out_info Receives the owning image record.
   * @return `true` when an owning image was found.
   */
  [[nodiscard]] bool find_by_pc(uintptr_t pc, elf_image_info &out_info) const noexcept {
    if (!vtbl_)
      return false;
    return vtbl_->find_by_pc(ctx_.get(), pc, out_info);
  }

  /**
   * @brief Reports whether the handle is bound to an enumerator.
   * @return `true` when the handle is valid.
   */
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return vtbl_ != nullptr; }

private:
  template <typename Tag, typename Context>
  static constexpr vtable s_vtbl{[](const void *c, span<elf_image_info> buf, size_t &count) noexcept {
                                   return static_cast<const Context *>(c)->enumerate(buf, count);
                                 },
                                 [](const void *c, uintptr_t pc, elf_image_info &info) noexcept {
                                   return static_cast<const Context *>(c)->find_by_pc(pc, info);
                                 }};

  template <typename Fn>
  static constexpr vtable s_fn_vtbl{[](const void *c, span<elf_image_info> buf, size_t &count) noexcept {
                                      return (*static_cast<const Fn *>(c)).enumerate(buf, count);
                                    },
                                    [](const void *c, uintptr_t pc, elf_image_info &info) noexcept {
                                      return (*static_cast<const Fn *>(c)).find_by_pc(pc, info);
                                    }};

  value_ptr<const void> ctx_{};
  const vtable *vtbl_{nullptr};
};

// ============================================================================
// Concrete Fixed-Capacity Multi-ELF Registry Context (Zero Allocation)
// ============================================================================

/**
 * @brief Fixed-capacity, zero-allocation registry of ELF images.
 *
 * @tparam MaxImages Maximum number of registered images.
 */
template <size_t MaxImages = 16> class multi_elf_registry_context {
public:
  /**
   * @brief Constructs an empty registry.
   */
  constexpr multi_elf_registry_context() noexcept = default;

  /**
   * @brief Registers an image, failing when the table is full.
   * @param info Image record to append.
   * @return `true` on success, `false` when at capacity.
   */
  constexpr bool register_image(const elf_image_info &info) noexcept {
    if (image_count_ >= MaxImages)
      return false;
    MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

    images_[image_count_++] = info;

    MICROFMT_END_UNSAFE_BUFFER_USAGE;

    return true;
  }

  /**
   * @brief Copies registered images into a caller buffer.
   * @param out_buffer Destination buffer.
   * @param out_count Receives the number of images stored.
   * @return Always `true`.
   */
  [[nodiscard]] constexpr bool enumerate(span<elf_image_info> out_buffer, size_t &out_count) const noexcept {
    size_t copy_count = (image_count_ < out_buffer.size()) ? image_count_ : out_buffer.size();
    for (size_t i = 0; i < copy_count; ++i) {
      MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

      out_buffer[i] = images_[i];

      MICROFMT_END_UNSAFE_BUFFER_USAGE;
    }
    out_count = copy_count;
    return true;
  }

  /**
   * @brief Locates the registered image containing @p pc.
   * @param pc Code address to look up.
   * @param out_info Receives the owning image record.
   * @return `true` when an owning image was found.
   */
  [[nodiscard]] constexpr bool find_by_pc(uintptr_t pc, elf_image_info &out_info) const noexcept {
    MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

    for (size_t i = 0; i < image_count_; ++i) {
      if (images_[i].contains(pc)) {
        out_info = images_[i];
        return true;
      }
    }

    MICROFMT_END_UNSAFE_BUFFER_USAGE;

    return false;
  }

  /**
   * @brief Returns the number of registered images.
   * @return Current image count.
   */
  [[nodiscard]] constexpr size_t size() const noexcept { return image_count_; }

private:
  /// Registered image records.
  elf_image_info images_[MaxImages]{};
  /// Number of occupied entries.
  size_t image_count_{0};
};

/**
 * @brief Tag identifying @ref multi_elf_registry_context in type-erased
 * handles.
 */
struct multi_elf_registry_tag {};

} // namespace microfmt