// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file remote_object.hpp @brief Remote object dumping, layout macros, and
 * string pointer wrappers with 32-bit compatibility support. */

#include "../microfmt.hpp"
#include "compat32.hpp"
#include "foreign_string_view.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace microfmt {

// ============================================================================
// Remote Object Field Metadata & Infrastructure
// ============================================================================

/**
 * @brief Type-erased function pointer for formatting a single remote object
 * field from a loaded local struct buffer.
 */
using remote_field_format_fn = void (*)(address_space_ref space, const void *field_ptr, span<char> work_buf,
                                        const sink &out) noexcept;

/**
 * @brief Static descriptor for a single field in a remote structure layout.
 */
struct remote_field_desc {
  const char *name;
  size_t offset;
  remote_field_format_fn format_fn;
};

// ============================================================================
// Remote Field Formatter Traits & Policies (Uniform Extension Point)
// ============================================================================

/**
 * @brief Default field formatter policy.
 * Can be specialized or overloaded for custom remote types, smart pointers,
 * etc.
 */
template <typename T, typename Enable = void> struct remote_field_traits {
  static void format(address_space_ref, const void *field_ptr, span<char>, const sink &out) noexcept {
    const auto &val = *static_cast<const T *>(field_ptr);
    formatter<T> fmt;
    fmt.format(val, out);
  }
};

class string_ptr; // forward declaration
template <> struct remote_field_traits<string_ptr> {
  static void format(address_space_ref space, const void *field_ptr, span<char> work_buf, const sink &out) noexcept;
};

class string32_ptr; // forward declaration
template <> struct remote_field_traits<string32_ptr> {
  static void format(address_space_ref space, const void *field_ptr, span<char> work_buf, const sink &out) noexcept;
};

// ============================================================================
// Remote String Pointer Wrappers (Synthesizing Foreign String Views)
// ============================================================================

/**
 * @brief 64-bit/Native remote string pointer wrapper.
 *
 * Stores an absolute address and synthesizes a `foreign_string_view` on demand
 * using caller-supplied scratch buffers.
 */
class string_ptr {
public:
  constexpr string_ptr() noexcept : addr_(0) {}
  constexpr explicit string_ptr(uintptr_t addr) noexcept : addr_(addr) {}
  explicit string_ptr(const char *ptr) noexcept : addr_(reinterpret_cast<uintptr_t>(ptr)) {}

  [[nodiscard]] constexpr uintptr_t address() const noexcept { return addr_; }
  [[nodiscard]] constexpr bool is_null() const noexcept { return addr_ == 0; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return addr_ != 0; }

  /**
   * @brief Synthesizes a @ref foreign_string_view over the pointed string.
   * @param space Address space handle.
   * @param scratch Scratch work buffer for chunked reading.
   * @param max_limit Maximum character render limit.
   */
  [[nodiscard]] constexpr foreign_string_view
  as_view(address_space_ref space, span<char> scratch MICROFMT_LIFETIMEBOUND, size_t max_limit = 4096) const noexcept {
    return foreign_string_view(addr_, space, scratch, max_limit);
  }

private:
  uintptr_t addr_{0};
};

// Implementations of string formatting traits
inline void remote_field_traits<string_ptr>::format(address_space_ref space, const void *field_ptr, span<char> work_buf,
                                                    const sink &out) noexcept {
  const auto &sptr = *static_cast<const string_ptr *>(field_ptr);
  if (sptr.is_null()) {
    out.write("(null)");
    return;
  }
  foreign_string_view fsv = sptr.as_view(space, work_buf);
  formatter<foreign_string_view> fmt;
  fmt.format(fsv, out);
}

static_assert(sizeof(string_ptr) == sizeof(void *), "string_ptr must have correct size");
static_assert(alignof(string_ptr) == sizeof(void *), "string32_ptr must be naturally aligned");

/**
 * @brief 32-bit compatibility string pointer wrapper (for 64-bit hosts).
 *
 * Stores a 32-bit address and synthesizes a `foreign_string_view` on demand
 * using caller-supplied scratch buffers.
 */
class alignas(uint32_t) string32_ptr {
public:
  constexpr string32_ptr() noexcept : addr_(0) {}
  constexpr explicit string32_ptr(uint32_t addr) noexcept : addr_(addr) {}

  [[nodiscard]] constexpr uint32_t raw_value() const noexcept { return addr_; }
  [[nodiscard]] constexpr uintptr_t address() const noexcept { return static_cast<uintptr_t>(addr_); }
  [[nodiscard]] constexpr bool is_null() const noexcept { return addr_ == 0; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return addr_ != 0; }

  /**
   * @brief Synthesizes a @ref foreign_string_view over the pointed string.
   * @param space Address space handle.
   * @param scratch Scratch work buffer for chunked reading.
   * @param max_limit Maximum character render limit.
   */
  [[nodiscard]] constexpr foreign_string_view
  as_view(address_space_ref space, span<char> scratch MICROFMT_LIFETIMEBOUND, size_t max_limit = 4096) const noexcept {
    return foreign_string_view(address(), space, scratch, max_limit);
  }

private:
  uint32_t addr_{0};
};

inline void remote_field_traits<string32_ptr>::format(address_space_ref space, const void *field_ptr,
                                                      span<char> work_buf, const sink &out) noexcept {
  const auto &sptr = *static_cast<const string32_ptr *>(field_ptr);
  if (sptr.is_null()) {
    out.write("(null)");
    return;
  }
  foreign_string_view fsv = sptr.as_view(space, work_buf);
  formatter<foreign_string_view> fmt;
  fmt.format(fsv, out);
}

static_assert(sizeof(string32_ptr) == 4, "string32_ptr must be exactly 4 bytes");
static_assert(alignof(string32_ptr) == 4, "string32_ptr must be 4-byte aligned");

// ============================================================================
// Formatters for string_ptr and string32_ptr
// ============================================================================

template <> struct formatter<string_ptr> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const string_ptr &ptr, const sink &out) const noexcept {
    if (ptr.is_null()) {
      out.write("(null)");
    } else {
      microfmt::format_to(out, MICROFMT_STRING("0x{:x}"), ptr.address());
    }
  }
};

template <> struct formatter<string32_ptr> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const string32_ptr &ptr, const sink &out) const noexcept {
    if (ptr.is_null()) {
      out.write("(null)");
    } else {
      microfmt::format_to(out, MICROFMT_STRING("{:#010x}"), ptr.raw_value());
    }
  }
};

/**
 * @brief Traits customization point for remote object metadata.
 */
template <typename T, typename Enable = void> struct remote_object_traits {
  static constexpr bool is_registered = false;
  static constexpr span<const remote_field_desc> fields() noexcept { return {}; }
  static constexpr size_t struct_size = 0;
  static constexpr size_t struct_align = 1;
};

// ============================================================================
// Remote Object View Container
// ============================================================================

// Lightweight type tag to pass types into constructor without instantiation
template <typename T> struct type_tag {};

class MICROFMT_POINTER remote_object_view {
public:
  template <typename T>
  constexpr remote_object_view(uintptr_t addr, address_space_ref space, type_tag<T>,
                               span<std::byte> scratch MICROFMT_LIFETIMEBOUND) noexcept
      : addr_(addr), space_(space), fields_(remote_object_traits<T>::fields()),
        struct_size_(remote_object_traits<T>::struct_size), struct_align_(remote_object_traits<T>::struct_align),
        scratch_(scratch) {
    static_assert(remote_object_traits<T>::is_registered, "Type T must be registered using MICROFMT_REMOTE_STRUCT");
  }

  constexpr remote_object_view(uintptr_t addr, address_space_ref space,
                               span<const remote_field_desc> fields MICROFMT_LIFETIMEBOUND, size_t struct_size,
                               size_t struct_align, span<std::byte> scratch MICROFMT_LIFETIMEBOUND) noexcept
      : addr_(addr), space_(space), fields_(fields), struct_size_(struct_size), struct_align_(struct_align),
        scratch_(scratch) {}

  [[nodiscard]] constexpr uintptr_t address() const noexcept { return addr_; }
  [[nodiscard]] constexpr address_space_ref space() const noexcept { return space_; }
  [[nodiscard]] constexpr span<const remote_field_desc> fields() const noexcept MICROFMT_LIFETIMEBOUND {
    return fields_;
  }
  [[nodiscard]] constexpr size_t struct_size() const noexcept { return struct_size_; }
  [[nodiscard]] constexpr span<std::byte> scratch() const noexcept MICROFMT_LIFETIMEBOUND { return scratch_; }
  [[nodiscard]] constexpr bool is_null() const noexcept { return addr_ == 0; }

  [[nodiscard]] expected<const void *, remote_load_error> load_raw() const noexcept {
    if (addr_ == 0)
      return unexpected(remote_load_error::null_address);
    if (scratch_.size() < struct_size_)
      return unexpected(remote_load_error::scratch_too_small);
    if (struct_align_ > 0 && reinterpret_cast<uintptr_t>(scratch_.data()) % struct_align_ != 0)
      return unexpected(remote_load_error::scratch_misaligned);
    if (!space_)
      return unexpected(remote_load_error::invalid_address_space);

    if (!space_.read_bytes(addr_, scratch_.data(), struct_size_))
      return unexpected(remote_load_error::read_failed);
    return static_cast<const void *>(scratch_.data());
  }

private:
  uintptr_t addr_{0};
  address_space_ref space_{};
  span<const remote_field_desc> fields_{};
  size_t struct_size_{0};
  size_t struct_align_{1};
  span<std::byte> scratch_{};
};

// ============================================================================
// Single Universal Formatter for remote_object_view (Zero Template Bloat)
// ============================================================================

template <> struct formatter<remote_object_view> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const remote_object_view &view, const sink &out) const noexcept {
    if (view.is_null()) {
      out.write("(null)");
      return;
    }

    auto obj = view.load_raw();
    if (!obj) {
      microfmt::format_to(out, MICROFMT_STRING("<fault@{:#x}>"), view.address());
      return;
    }

    out.write("{ ");
    auto fields = view.fields();

    auto scratch = view.scratch();
    span<char> work_buf;
    if (scratch.size() > view.struct_size()) {
      MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

      work_buf = span<char>(reinterpret_cast<char *>(scratch.data() + view.struct_size()),
                            scratch.size() - view.struct_size());

      MICROFMT_END_UNSAFE_BUFFER_USAGE;
    }

    for (size_t i = 0; i < fields.size(); ++i) {
      if (i > 0) {
        out.write(", ");
      }
      out.write(fields[i].name);
      out.write(": ");

      MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

      const void *field_ptr = reinterpret_cast<const char *>(*obj) + fields[i].offset;

      MICROFMT_END_UNSAFE_BUFFER_USAGE;

      fields[i].format_fn(view.space(), field_ptr, work_buf, out);
    }
    out.write(" }");
  }
};

} // namespace microfmt

// ============================================================================
// Begin / Field / End Macro DSL for External/Caller-Defined Remote Structures
// ============================================================================

#define MICROFMT_REMOTE_STRUCT_BEGIN(StructName)                                                                       \
  template <> struct microfmt::remote_object_traits<StructName> {                                                      \
    static constexpr size_t struct_size = sizeof(StructName);                                                          \
    static constexpr size_t struct_align = alignof(StructName);                                                        \
    static constexpr bool is_registered = true;                                                                        \
    static microfmt::span<const remote_field_desc> fields() noexcept {                                                 \
      using CurrentStruct = StructName;                                                                                \
      static constexpr microfmt::remote_field_desc static_fields[] = {

#define MICROFMT_REMOTE_FIELD(Name, ...)                                                                               \
  {#Name, offsetof(CurrentStruct, Name), &microfmt::remote_field_traits<__VA_ARGS__>::format},

#define MICROFMT_REMOTE_STRUCT_END()                                                                                   \
  }                                                                                                                    \
  ;                                                                                                                    \
  return {static_fields, sizeof(static_fields) / sizeof(static_fields[0])};                                            \
  }                                                                                                                    \
  }                                                                                                                    \
  ;
