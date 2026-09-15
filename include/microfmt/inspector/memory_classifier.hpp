// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file memory_classifier.hpp
 * @brief Type-erased memory region classifier for system and kernel
 * introspection. */

#include "address_space.hpp"
#include <cstdint>
#include <type_traits>

namespace microfmt {

/**
 * @brief Enumeration of classified memory region types.
 */
enum class memory_region_type : uint8_t {
  unknown = 0,
  kernel_code,    // Kernel text / executable code section
  kernel_data,    // Kernel initialized data (.data) and bss (.bss)
  kernel_stack,   // Kernel thread / exception stack
  process_stack,  // User-space thread stack
  direct_map,     // Physical-to-virtual direct mapping region (e.g., __va /
                  // PAGE_OFFSET)
  struct_pages,   // Array of page descriptors (e.g., Linux mem_map / page
                  // structs)
  kernel_heap,    // Kernel allocator pool (e.g., slab / slub / malloc)
  user_code,      // User-space text segment
  user_data,      // User-space data and heap
  device_mmio,    // Memory-Mapped I/O registers
  guard_page,     // Unmapped guard page for stack overflow detection
  ns_map,         // NonSecure Mapping on ARM TrustZone
  ns_device_mmio, // NonSecure Device mapping on ARM TrustZone
};

/**
 * @brief Detailed description of a classified memory region or address.
 */
struct memory_region_info {
  /// Start virtual address of the region.
  uintptr_t start_address{0};

  /// End virtual address of the region (exclusive).
  uintptr_t end_address{0};

  /// Classification type of the region.
  memory_region_type type{memory_region_type::unknown};

  /// Associated address space ID / ASID / PID (if applicable).
  uint32_t space_id{0};

  /// Page protection flags.
  bool readable{false};
  bool writable{false};
  bool executable{false};

  /**
   * @brief Checks if a given virtual address falls within this region.
   */
  [[nodiscard]] constexpr bool contains(uintptr_t addr) const noexcept {
    return addr >= start_address && addr < end_address;
  }
};

/**
 * @brief Static customization point describing a memory classifier backend.
 * @tparam Tag Tag identifying the classifier implementation.
 */
template <typename Tag> struct memory_classifier_traits;

/**
 * @brief Type-erased, two-word handle for classifying memory regions and
 * addresses.
 *
 * Packs a context pointer and a virtual table into two words, avoiding
 * allocations, RTTI, and virtual dispatch.
 */
class memory_classifier_ref {
public:
  /**
   * @brief Virtual table of memory classification operations.
   */
  struct vtable {
    /**
     * @brief Classifies a specific virtual address.
     * @param ctx Opaque pointer to concrete context state.
     * @param virt_addr Virtual address to query.
     * @param out_info Receives the resolved region boundaries and
     * classification type.
     * @return `true` if the address was successfully classified, `false`
     * otherwise.
     */
    bool (*classify_address)(const void *ctx, uintptr_t virt_addr,
                             memory_region_info &out_info) noexcept;
  };

  /**
   * @brief Constructs an empty (invalid) handle.
   */
  constexpr memory_classifier_ref() noexcept = default;

  /**
   * @brief Constructs a handle for a stateless classifier tag.
   */
  template <
      typename Tag, typename Traits = memory_classifier_traits<Tag>,
      std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  constexpr explicit memory_classifier_ref(Tag) noexcept
      : ctx_(nullptr), vtbl_(&s_vtbl<Tag>) {}

  /**
   * @brief Constructs a handle for a stateful classifier tag.
   */
  template <typename Tag, typename Context,
            typename Traits = memory_classifier_traits<Tag>,
            std::enable_if_t<
                !std::is_void_v<typename Traits::context_type> &&
                    std::is_convertible_v<
                        const Context *, const typename Traits::context_type *>,
                int> = 0>
  constexpr memory_classifier_ref(Tag, const Context &ctx) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag>) {}

  template <
      typename Tag, typename Traits = memory_classifier_traits<Tag>,
      std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr memory_classifier_ref make() noexcept {
    return memory_classifier_ref(Tag{});
  }

  template <
      typename Tag, typename Context,
      typename Traits = memory_classifier_traits<Tag>,
      std::enable_if_t<!std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr memory_classifier_ref
  make(const Context &ctx) noexcept {
    return memory_classifier_ref(Tag{}, ctx);
  }

  /**
   * @brief Classifies a target virtual address.
   * @param virt_addr Virtual address to inspect.
   * @param out_info Receives metadata about the memory region.
   * @return `true` on successful classification, `false` otherwise.
   */
  [[nodiscard]] bool
  classify_address(uintptr_t virt_addr,
                   memory_region_info &out_info) const noexcept {
    if (!vtbl_ || !vtbl_->classify_address)
      return false;
    return vtbl_->classify_address(ctx_, virt_addr, out_info);
  }

  /**
   * @brief Reports whether the handle is bound to a classifier.
   */
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return vtbl_ != nullptr;
  }

private:
  template <typename Tag>
  static constexpr vtable s_vtbl{
      &memory_classifier_traits<Tag>::classify_address};

  const void *ctx_{nullptr};
  const vtable *vtbl_{nullptr};
};

} // namespace microfmt