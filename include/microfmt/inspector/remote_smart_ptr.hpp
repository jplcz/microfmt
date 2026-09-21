// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file remote_smart_ptr.hpp
 * @brief Remote smart-pointer views, layout wrappers, and formatters. */

#include "address_space.hpp"
#include "remote_layout_accessor.hpp"
#include "remote_object.hpp"
#include <cstddef>
#include <cstdint>

namespace microfmt {

// ============================================================================
// Unique Pointer View & Traits
// ============================================================================

/**
 * @brief Formattable view over a unique pointer stored in a remote process.
 * @tparam T Pointee type.
 * @tparam RemotePtr Pointer representation in the target process.
 */
template <typename T, typename RemotePtr = uintptr_t>
class RELOCO_POINTER remote_unique_ptr_view {
  using pointer_query =
      decltype(make_remote_offset_query<uintptr_t, RemotePtr>(0));

public:
  /**
   * @brief Constructs a view over a remote unique-pointer object.
   * @param ptr_addr Address of the remote pointer object.
   * @param space Address space containing the pointer and pointee.
   * @param scratch Storage used to load the pointee.
   * @param ptr_offset Offset from @p ptr_addr to the stored pointer.
   */
  constexpr remote_unique_ptr_view(uintptr_t ptr_addr, address_space_ref space,
                                   span<std::byte> scratch
                                       RELOCO_LIFETIMEBOUND,
                                   ptrdiff_t ptr_offset = 0) noexcept
      : ptr_addr_(ptr_addr), space_(space), scratch_(scratch),
        pointer_(make_remote_offset_query<uintptr_t, RemotePtr>(ptr_offset)) {}

  /**
   * @brief Loads and renders the pointee or `nullptr`.
   * @param out Destination sink.
   * @return `true` on success; `false` when a required remote read fails.
   */
  bool format(const sink &out) const noexcept {
    uintptr_t obj_addr = 0;
    if (!pointer_(space_, ptr_addr_, obj_addr)) {
      return false;
    }

    if (obj_addr == 0) {
      out.write("nullptr");
      return true;
    }

    if constexpr (remote_object_traits<T>::is_registered) {
      remote_object_view obj_view(obj_addr, space_, type_tag<T>{}, scratch_);
      formatter<remote_object_view>().format(obj_view, out);
    } else {
      scratch_allocator allocator(scratch_);
      T *val_ptr = allocator.allocate<T>();
      if (!val_ptr)
        return false;
      if (!space_.read_bytes(obj_addr, val_ptr, sizeof(T)))
        return false;
      formatter<T>().format(*val_ptr, out);
    }
    return true;
  }

private:
  uintptr_t ptr_addr_;
  address_space_ref space_;
  span<std::byte> scratch_;
  pointer_query pointer_;
};

// ============================================================================
// Shared Pointer View & Traits (Control Block Reference Counting)
// ============================================================================

/**
 * @brief Formattable view over a shared pointer and its remote control block.
 * @tparam T Pointee type.
 * @tparam RemotePtr Pointer representation in the target process.
 * @tparam RemoteRefCount Reference-count representation in the target process.
 */
template <typename T, typename RemotePtr = uintptr_t,
          typename RemoteRefCount = int32_t>
class RELOCO_POINTER remote_shared_ptr_view {
  using pointer_query =
      decltype(make_remote_offset_query<uintptr_t, RemotePtr>(0));
  using count_query =
      decltype(make_remote_offset_query<RemoteRefCount, RemoteRefCount>(0));

public:
  /**
   * @brief Constructs a view over a remote shared-pointer object.
   * @param shared_ptr_addr Address of the remote shared-pointer object.
   * @param space Address space containing the pointer, pointee, and control
   * block.
   * @param scratch Storage used to load the pointee.
   * @param ptr_offset Offset to the stored pointee pointer.
   * @param control_block_offset Offset to the control-block pointer.
   * @param use_count_offset Offset in the control block to the strong count.
   * @param weak_count_offset Offset in the control block to the weak count.
   */
  constexpr remote_shared_ptr_view(uintptr_t shared_ptr_addr,
                                   address_space_ref space,
                                   span<std::byte> scratch
                                       RELOCO_LIFETIMEBOUND,
                                   ptrdiff_t ptr_offset,
                                   ptrdiff_t control_block_offset,
                                   ptrdiff_t use_count_offset,
                                   ptrdiff_t weak_count_offset) noexcept
      : addr_(shared_ptr_addr), space_(space), scratch_(scratch),
        pointer_(make_remote_offset_query<uintptr_t, RemotePtr>(ptr_offset)),
        control_block_(make_remote_offset_query<uintptr_t, RemotePtr>(
            control_block_offset)),
        use_count_(make_remote_offset_query<RemoteRefCount, RemoteRefCount>(
            use_count_offset)),
        weak_count_(make_remote_offset_query<RemoteRefCount, RemoteRefCount>(
            weak_count_offset)) {}

  /**
   * @brief Loads and renders the pointee and reference counts.
   * @param out Destination sink.
   * @return `true` on success; `false` when required pointer reads fail.
   */
  bool format(const sink &out) const noexcept {
    uintptr_t obj_addr = 0;
    if (!pointer_(space_, addr_, obj_addr)) {
      return false;
    }

    if (obj_addr == 0) {
      out.write("shared_ptr(nullptr)");
      return true;
    }

    uintptr_t cb_addr = 0;
    if (!control_block_(space_, addr_, cb_addr)) {
      return false;
    }

    RemoteRefCount use_cnt = -1;
    RemoteRefCount weak_cnt = -1;

    if (cb_addr != 0) {
      std::ignore = use_count_(space_, cb_addr, use_cnt);
      std::ignore = weak_count_(space_, cb_addr, weak_cnt);
    }

    out.write("shared_ptr(");

    if constexpr (remote_object_traits<T>::is_registered) {
      remote_object_view obj_view(obj_addr, space_, type_tag<T>{}, scratch_);
      formatter<remote_object_view>().format(obj_view, out);
    } else {
      scratch_allocator allocator(scratch_);
      if (T *val_ptr = allocator.allocate<T>()) {
        if (space_.read_bytes(obj_addr, val_ptr, sizeof(T))) {
          formatter<T>().format(*val_ptr, out);
        } else {
          out.write("<fault>");
        }
      }
    }

    microfmt::format_to(out, ", use_count={}, weak_count={})",
                        static_cast<int64_t>(use_cnt),
                        static_cast<int64_t>(weak_cnt));
    return true;
  }

private:
  uintptr_t addr_;
  address_space_ref space_;
  span<std::byte> scratch_;
  pointer_query pointer_;
  pointer_query control_block_;
  count_query use_count_;
  count_query weak_count_;
};

// ============================================================================
// Intrusive Pointer View & Traits (Embedded Reference Counting)
// ============================================================================

/**
 * @brief Formattable view over an intrusive pointer with an embedded count.
 * @tparam T Pointee type.
 * @tparam RemotePtr Pointer representation in the target process.
 * @tparam RemoteRefCount Reference-count representation in the target process.
 */
template <typename T, typename RemotePtr = uintptr_t,
          typename RemoteRefCount = int32_t>
class RELOCO_POINTER remote_intrusive_ptr_view {
  using pointer_query =
      decltype(make_remote_offset_query<uintptr_t, RemotePtr>(0));
  using count_query =
      decltype(make_remote_offset_query<RemoteRefCount, RemoteRefCount>(0));

public:
  /**
   * @brief Constructs a view over a remote intrusive-pointer object.
   * @param intrusive_ptr_addr Address of the remote pointer object.
   * @param space Address space containing the pointer and pointee.
   * @param scratch Storage used to load the pointee.
   * @param ptr_offset Offset to the stored pointee pointer.
   * @param ref_count_offset Offset in the pointee to its reference count.
   */
  constexpr remote_intrusive_ptr_view(uintptr_t intrusive_ptr_addr,
                                      address_space_ref space,
                                      span<std::byte> scratch
                                          RELOCO_LIFETIMEBOUND,
                                      ptrdiff_t ptr_offset,
                                      ptrdiff_t ref_count_offset) noexcept
      : addr_(intrusive_ptr_addr), space_(space), scratch_(scratch),
        pointer_(make_remote_offset_query<uintptr_t, RemotePtr>(ptr_offset)),
        ref_count_(
            make_remote_offset_query<RemoteRefCount, RemoteRefCount>(
                ref_count_offset)) {}

  /**
   * @brief Loads and renders the pointee and its embedded reference count.
   * @param out Destination sink.
   * @return `true` on success; `false` when the pointer read fails.
   */
  bool format(const sink &out) const noexcept {
    uintptr_t obj_addr = 0;
    if (!pointer_(space_, addr_, obj_addr)) {
      return false;
    }

    if (obj_addr == 0) {
      out.write("intrusive_ptr(nullptr)");
      return true;
    }

    RemoteRefCount ref_cnt = 0;
    std::ignore = ref_count_(space_, obj_addr, ref_cnt);

    out.write("intrusive_ptr(");

    if constexpr (remote_object_traits<T>::is_registered) {
      remote_object_view obj_view(obj_addr, space_, type_tag<T>{}, scratch_);
      formatter<remote_object_view>().format(obj_view, out);
    } else {
      scratch_allocator allocator(scratch_);
      if (T *val_ptr = allocator.allocate<T>()) {
        if (space_.read_bytes(obj_addr, val_ptr, sizeof(T))) {
          formatter<T>().format(*val_ptr, out);
        } else {
          out.write("<fault>");
        }
      }
    }

    microfmt::format_to(out, ", ref_count={})", ref_cnt);
    return true;
  }

private:
  uintptr_t addr_;
  address_space_ref space_;
  span<std::byte> scratch_;
  pointer_query pointer_;
  count_query ref_count_;
};

// ============================================================================
// Inline Smart Pointer Wrapper Classes
// ============================================================================

/**
 * @brief Layout wrapper for a remote unique pointer.
 * @tparam T Pointee type.
 * @tparam RemotePtr Pointer representation in the target process.
 */
template <typename T, typename RemotePtr = uintptr_t> class remote_unique_ptr {
public:
  constexpr remote_unique_ptr() noexcept : ptr_(0) {}
  constexpr explicit remote_unique_ptr(RemotePtr ptr) noexcept : ptr_(ptr) {}

  [[nodiscard]] constexpr uintptr_t address() const noexcept {
    return static_cast<uintptr_t>(ptr_);
  }
  [[nodiscard]] constexpr bool is_null() const noexcept { return ptr_ == 0; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return ptr_ != 0;
  }

private:
  RemotePtr ptr_{0};
  template <typename U, typename RP> friend struct remote_field_traits;
};

/**
 * @brief Layout wrapper for a remote shared pointer.
 * @tparam T Pointee type.
 * @tparam RemotePtr Pointer representation in the target process.
 * @tparam RemoteRefCount Reference-count representation in the target process.
 */
template <typename T, typename RemotePtr = uintptr_t,
          typename RemoteRefCount = int32_t>
class remote_shared_ptr {
public:
  constexpr remote_shared_ptr() noexcept : ptr_(0), control_block_(0) {}

  [[nodiscard]] constexpr uintptr_t address() const noexcept {
    return static_cast<uintptr_t>(ptr_);
  }
  [[nodiscard]] constexpr uintptr_t control_block_address() const noexcept {
    return static_cast<uintptr_t>(control_block_);
  }
  [[nodiscard]] constexpr bool is_null() const noexcept { return ptr_ == 0; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return ptr_ != 0;
  }

private:
  RemotePtr ptr_{0};
  RemotePtr control_block_{0};
};

/**
 * @brief Layout wrapper for a remote intrusive pointer.
 * @tparam T Pointee type.
 * @tparam RefCountOffset Offset in the pointee to its reference count.
 * @tparam RemotePtr Pointer representation in the target process.
 * @tparam RemoteRefCount Reference-count representation in the target process.
 */
template <typename T, ptrdiff_t RefCountOffset = 0,
          typename RemotePtr = uintptr_t, typename RemoteRefCount = int32_t>
class remote_intrusive_ptr {
public:
  constexpr remote_intrusive_ptr() noexcept : ptr_(0) {}
  constexpr explicit remote_intrusive_ptr(RemotePtr ptr) noexcept : ptr_(ptr) {}

  [[nodiscard]] constexpr uintptr_t address() const noexcept {
    return static_cast<uintptr_t>(ptr_);
  }
  [[nodiscard]] constexpr bool is_null() const noexcept { return ptr_ == 0; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return ptr_ != 0;
  }

  static constexpr ptrdiff_t ref_count_offset() noexcept {
    return RefCountOffset;
  }

private:
  RemotePtr ptr_{0};
};

// ============================================================================
// remote_field_traits Specializations
// ============================================================================

template <typename T, typename RemotePtr>
struct remote_field_traits<remote_unique_ptr<T, RemotePtr>> {
  static void format(address_space_ref space, const void *field_ptr,
                     span<char> work_buf, const sink &out) noexcept {
    const auto &uptr =
        *static_cast<const remote_unique_ptr<T, RemotePtr> *>(field_ptr);
    uintptr_t obj_addr = uptr.address();
    if (obj_addr == 0) {
      out.write("nullptr");
      return;
    }
    span<std::byte> scratch_bytes(
        reinterpret_cast<std::byte *>(work_buf.data()), work_buf.size());
    remote_unique_ptr_view<T, RemotePtr> view(
        reinterpret_cast<uintptr_t>(field_ptr), space, scratch_bytes, 0);
    view.format(out);
  }
};

template <typename T, typename RemotePtr, typename RemoteRefCount>
struct remote_field_traits<remote_shared_ptr<T, RemotePtr, RemoteRefCount>> {
  static void format(address_space_ref space, const void *field_ptr,
                     span<char> work_buf, const sink &out) noexcept {
    const auto &sptr =
        *static_cast<const remote_shared_ptr<T, RemotePtr, RemoteRefCount> *>(
            field_ptr);
    if (sptr.is_null()) {
      out.write("shared_ptr(nullptr)");
      return;
    }
    span<std::byte> scratch_bytes(
        reinterpret_cast<std::byte *>(work_buf.data()), work_buf.size());
    remote_shared_ptr_view<T, RemotePtr, RemoteRefCount> view(
        reinterpret_cast<uintptr_t>(field_ptr), space, scratch_bytes, 0,
        sizeof(RemotePtr), 0, sizeof(RemoteRefCount));
    view.format(out);
  }
};

template <typename T, ptrdiff_t RefCountOffset, typename RemotePtr,
          typename RemoteRefCount>
struct remote_field_traits<
    remote_intrusive_ptr<T, RefCountOffset, RemotePtr, RemoteRefCount>> {
  static void format(address_space_ref space, const void *field_ptr,
                     span<char> work_buf, const sink &out) noexcept {
    const auto &iptr =
        *static_cast<const remote_intrusive_ptr<T, RefCountOffset, RemotePtr,
                                                RemoteRefCount> *>(field_ptr);
    if (iptr.is_null()) {
      out.write("intrusive_ptr(nullptr)");
      return;
    }

    span<std::byte> scratch_bytes(
        reinterpret_cast<std::byte *>(work_buf.data()), work_buf.size());
    remote_intrusive_ptr_view<T, RemotePtr, RemoteRefCount> view(
        reinterpret_cast<uintptr_t>(field_ptr), space, scratch_bytes, 0,
        RefCountOffset);
    view.format(out);
  }
};

// ============================================================================
// Formatter Metadata Specializations
// ============================================================================

template <typename T, typename RP> struct formatter<remote_unique_ptr<T, RP>> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const remote_unique_ptr<T, RP> &ptr,
              const sink &out) const noexcept {
    if (ptr.is_null()) {
      out.write("nullptr");
    } else {
      microfmt::format_to(out, "0x{:x}", ptr.address());
    }
  }
};

template <typename T, typename RP, typename RRC>
struct formatter<remote_shared_ptr<T, RP, RRC>> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const remote_shared_ptr<T, RP, RRC> &ptr,
              const sink &out) const noexcept {
    if (ptr.is_null()) {
      out.write("shared_ptr(nullptr)");
    } else {
      microfmt::format_to(out, "0x{:x}", ptr.address());
    }
  }
};

template <typename T, ptrdiff_t RO, typename RP, typename RRC>
struct formatter<remote_intrusive_ptr<T, RO, RP, RRC>> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const remote_intrusive_ptr<T, RO, RP, RRC> &ptr,
              const sink &out) const noexcept {
    if (ptr.is_null()) {
      out.write("intrusive_ptr(nullptr)");
    } else {
      microfmt::format_to(out, "0x{:x}", ptr.address());
    }
  }
};

} // namespace microfmt

// ============================================================================
// Formatter Specializations for microfmt::print Integration
// ============================================================================

template <typename T, typename RemotePtr>
struct microfmt::formatter<microfmt::remote_unique_ptr_view<T, RemotePtr>> {
  constexpr void parse(microfmt::format_parse_context &) noexcept {}

  void format(const microfmt::remote_unique_ptr_view<T, RemotePtr> &view,
              const microfmt::sink &out) const noexcept {
    if (!view.format(out)) {
      out.write("<fault>");
    }
  }
};

template <typename T, typename RemotePtr, typename RemoteRefCount>
struct microfmt::formatter<
    microfmt::remote_shared_ptr_view<T, RemotePtr, RemoteRefCount>> {
  constexpr void parse(microfmt::format_parse_context &) noexcept {}

  void format(const microfmt::remote_shared_ptr_view<T, RemotePtr,
                                                     RemoteRefCount> &view,
              const microfmt::sink &out) const noexcept {
    if (!view.format(out)) {
      out.write("<fault>");
    }
  }
};

template <typename T, typename RemotePtr, typename RemoteRefCount>
struct microfmt::formatter<
    microfmt::remote_intrusive_ptr_view<T, RemotePtr, RemoteRefCount>> {
  constexpr void parse(microfmt::format_parse_context &) noexcept {}

  void format(const microfmt::remote_intrusive_ptr_view<T, RemotePtr,
                                                        RemoteRefCount> &view,
              const microfmt::sink &out) const noexcept {
    if (!view.format(out)) {
      out.write("<fault>");
    }
  }
};