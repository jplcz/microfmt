#pragma once

#include "address_space.hpp"
#include "remote_object.hpp"
#include <cstddef>
#include <cstdint>

namespace microfmt {

// ============================================================================
// Unique Pointer View & Traits
// ============================================================================

template <typename T, typename RemotePtr = uintptr_t>
class remote_unique_ptr_view {
public:
  constexpr remote_unique_ptr_view(uintptr_t ptr_addr, address_space_ref space,
                                   span<std::byte> scratch,
                                   ptrdiff_t ptr_offset = 0) noexcept
      : ptr_addr_(ptr_addr), space_(space), scratch_(scratch),
        ptr_offset_(ptr_offset) {}

  bool format(const sink &out) const noexcept {
    uintptr_t raw_ptr_addr = ptr_addr_ + ptr_offset_;
    RemotePtr remote_ptr{};
    if (!space_.read(raw_ptr_addr, remote_ptr)) {
      return false;
    }

    uintptr_t obj_addr = static_cast<uintptr_t>(remote_ptr);
    if (obj_addr == 0) {
      out.write("nullptr");
      return true;
    }

    if constexpr (remote_object_traits<T>::is_registered) {
      remote_object_view obj_view(obj_addr, space_, type_tag<T>{}, scratch_);
      formatter<remote_object_view>().format(obj_view, out);
    } else {
      if (scratch_.size() < sizeof(T))
        return false;
      T *val_ptr = reinterpret_cast<T *>(scratch_.data());
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
  ptrdiff_t ptr_offset_;
};

// ============================================================================
// Shared Pointer View & Traits (Control Block Reference Counting)
// ============================================================================

template <typename T, typename RemotePtr = uintptr_t,
          typename RemoteRefCount = int32_t>
class remote_shared_ptr_view {
public:
  constexpr remote_shared_ptr_view(uintptr_t shared_ptr_addr,
                                   address_space_ref space,
                                   span<std::byte> scratch,
                                   ptrdiff_t ptr_offset,
                                   ptrdiff_t control_block_offset,
                                   ptrdiff_t use_count_offset,
                                   ptrdiff_t weak_count_offset) noexcept
      : addr_(shared_ptr_addr), space_(space), scratch_(scratch),
        ptr_off_(ptr_offset), cb_off_(control_block_offset),
        use_off_(use_count_offset), weak_off_(weak_count_offset) {}

  bool format(const sink &out) const noexcept {
    RemotePtr remote_ptr{};
    if (!space_.read(addr_ + ptr_off_, remote_ptr)) {
      return false;
    }

    uintptr_t obj_addr = static_cast<uintptr_t>(remote_ptr);
    if (obj_addr == 0) {
      out.write("shared_ptr(nullptr)");
      return true;
    }

    RemotePtr control_ptr{};
    if (!space_.read(addr_ + cb_off_, control_ptr)) {
      return false;
    }
    uintptr_t cb_addr = static_cast<uintptr_t>(control_ptr);

    RemoteRefCount use_cnt = -1;
    RemoteRefCount weak_cnt = -1;

    if (cb_addr != 0) {
      std::ignore = space_.read(cb_addr + use_off_, use_cnt);
      std::ignore = space_.read(cb_addr + weak_off_, weak_cnt);
    }

    out.write("shared_ptr(");

    if constexpr (remote_object_traits<T>::is_registered) {
      remote_object_view obj_view(obj_addr, space_, type_tag<T>{}, scratch_);
      formatter<remote_object_view>().format(obj_view, out);
    } else {
      if (scratch_.size() >= sizeof(T)) {
        T *val_ptr = reinterpret_cast<T *>(scratch_.data());
        if (space_.read_bytes(obj_addr, val_ptr, sizeof(T))) {
          formatter<T>().format(*val_ptr, out);
        } else {
          out.write("<fault>");
        }
      }
    }

    microfmt::format_to(out, MICROFMT_STRING(", use_count={}, weak_count={})"),
                        static_cast<int64_t>(use_cnt),
                        static_cast<int64_t>(weak_cnt));
    return true;
  }

private:
  uintptr_t addr_;
  address_space_ref space_;
  span<std::byte> scratch_;
  ptrdiff_t ptr_off_;
  ptrdiff_t cb_off_;
  ptrdiff_t use_off_;
  ptrdiff_t weak_off_;
};

// ============================================================================
// Intrusive Pointer View & Traits (Embedded Reference Counting)
// ============================================================================

template <typename T, typename RemotePtr = uintptr_t,
          typename RemoteRefCount = int32_t>
class remote_intrusive_ptr_view {
public:
  constexpr remote_intrusive_ptr_view(uintptr_t intrusive_ptr_addr,
                                      address_space_ref space,
                                      span<std::byte> scratch,
                                      ptrdiff_t ptr_offset,
                                      ptrdiff_t ref_count_offset) noexcept
      : addr_(intrusive_ptr_addr), space_(space), scratch_(scratch),
        ptr_off_(ptr_offset), ref_off_(ref_count_offset) {}

  bool format(const sink &out) const noexcept {
    RemotePtr remote_ptr{};
    if (!space_.read(addr_ + ptr_off_, remote_ptr)) {
      return false;
    }

    uintptr_t obj_addr = static_cast<uintptr_t>(remote_ptr);
    if (obj_addr == 0) {
      out.write("intrusive_ptr(nullptr)");
      return true;
    }

    RemoteRefCount ref_cnt = 0;
    std::ignore = space_.read(obj_addr + ref_off_, ref_cnt);

    out.write("intrusive_ptr(");

    if constexpr (remote_object_traits<T>::is_registered) {
      remote_object_view obj_view(obj_addr, space_, type_tag<T>{}, scratch_);
      formatter<remote_object_view>().format(obj_view, out);
    } else {
      if (scratch_.size() >= sizeof(T)) {
        T *val_ptr = reinterpret_cast<T *>(scratch_.data());
        if (space_.read_bytes(obj_addr, val_ptr, sizeof(T))) {
          formatter<T>().format(*val_ptr, out);
        } else {
          out.write("<fault>");
        }
      }
    }

    microfmt::format_to(out, MICROFMT_STRING(", ref_count={})"), ref_cnt);
    return true;
  }

private:
  uintptr_t addr_;
  address_space_ref space_;
  span<std::byte> scratch_;
  ptrdiff_t ptr_off_;
  ptrdiff_t ref_off_;
};

// ============================================================================
// Inline Smart Pointer Wrapper Classes
// ============================================================================

/**
 * @brief Inline wrapper for remote unique pointers.
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
 * @brief Inline wrapper for remote shared pointers (stores object ptr and
 * control block ptr).
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