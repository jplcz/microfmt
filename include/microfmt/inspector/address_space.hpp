// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

/** @file address_space.hpp @brief Type-erased remote address-space access and
 * remote string/object views. */

#include "../microfmt.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>
namespace microfmt {

// ============================================================================
// Static Customization Point
// ============================================================================

/**
 * @brief Static customization point describing a remote address space.
 *
 * Specialize for a tag type to provide `context_type` plus `read_bytes` and
 * `read_string` implementations.
 *
 * @tparam Tag Tag identifying the address-space implementation.
 */
template <typename Tag> struct address_space_traits;

/**
 * @brief Tag for the built-in local/self address space.
 *
 * Used when the visited memory belongs to the currently executing process.
 */
struct local_space_tag {};

/**
 * @brief Traits for the built-in @ref local_space_tag.
 *
 * Reads directly through ordinary pointers; the context type is `void`.
 */
template <> struct address_space_traits<local_space_tag> {
  using context_type = void;

  /**
   * @brief Copies raw bytes from `addr` into `dest`.
   * @param Unused context pointer.
   * @param addr Absolute source address.
   * @param dest Destination buffer.
   * @param size Number of bytes to copy.
   * @return `true` on success, `false` for a null address.
   */
  static bool read_bytes(const void *, uintptr_t addr, void *dest,
                         size_t size) noexcept {
    if (addr == 0)
      return false;
    std::memcpy(dest, reinterpret_cast<const void *>(addr), size);
    return true;
  }

  /**
   * @brief Reads a string until a null terminator or the length limit.
   * @param Unused context pointer.
   * @param addr Absolute source address.
   * @param dest Destination buffer.
   * @param max_len Maximum characters that fit in @p dest.
   * @param out_len Receives the number of characters read.
   * @param null_term Receives whether a null terminator was encountered.
   * @return `true` on success, `false` for a null address.
   */
  static bool read_string(const void *, uintptr_t addr, char *dest,
                          size_t max_len, size_t &out_len,
                          bool &null_term) noexcept {
    if (addr == 0)
      return false;
    const auto *src = reinterpret_cast<const char *>(addr);
    size_t i = 0;
    while (i < max_len) {
      dest[i] = src[i];
      if (dest[i] == '\0') {
        out_len = i;
        null_term = true;
        return true;
      }
      ++i;
    }
    out_len = max_len;
    null_term = false;
    return true;
  }
};

// ============================================================================
// Minimal-Stack Type-Erased Transport (2 Words: ctx_ + vtbl_)
// ============================================================================

/**
 * @brief Type-erased, two-word handle to a remote address space.
 *
 * Packs a context pointer and a virtual table into two words, avoiding
 * allocations, RTTI, and virtual dispatch.
 */
class address_space_ref {
public:
  /**
   * @brief Virtual table of address-space read operations.
   */
  struct vtable {
    /**
     * @brief Reads raw bytes. See @ref address_space_ref::read_bytes.
     */
    bool (*read_bytes)(const void *ctx, uintptr_t addr, void *dest,
                       size_t size) noexcept;
    /**
     * @brief Reads a string. See @ref address_space_ref::read_string_chunk.
     */
    bool (*read_string)(const void *ctx, uintptr_t addr, char *dest,
                        size_t max_len, size_t &out_len,
                        bool &null_term) noexcept;
  };

  /**
   * @brief Constructs an empty (invalid) handle.
   */
  constexpr address_space_ref() noexcept = default;

  /**
   * @brief Constructs a handle for a stateless address-space tag.
   * @tparam Tag Address-space tag type.
   * @tparam Traits Specialized traits, enabled when `context_type` is `void`.
   * @param Tag Value used to select the traits.
   */
  template <
      typename Tag, typename Traits = address_space_traits<Tag>,
      std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  constexpr explicit address_space_ref(Tag) noexcept
      : ctx_(nullptr), vtbl_(&s_vtbl<Tag>) {}

  /**
   * @brief Constructs a handle for a stateful address-space tag.
   * @tparam Tag Address-space tag type.
   * @tparam Context Concrete context type.
   * @tparam Traits Specialized traits, enabled when `context_type` is
   * non-void and @p Context converts to it.
   * @param Tag Value used to select the traits.
   * @param ctx Context object providing the reads.
   */
  template <typename Tag, typename Context,
            typename Traits = address_space_traits<Tag>,
            std::enable_if_t<
                !std::is_void_v<typename Traits::context_type> &&
                    std::is_convertible_v<
                        const Context *, const typename Traits::context_type *>,
                int> = 0>
  constexpr address_space_ref(Tag, const Context &ctx) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag>) {}

  /**
   * @brief Creates a handle for a stateless address-space tag.
   * @tparam Tag Address-space tag type.
   * @tparam Traits Specialized traits, enabled when `context_type` is `void`.
   * @return An @ref address_space_ref for the tag.
   */
  template <
      typename Tag, typename Traits = address_space_traits<Tag>,
      std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr address_space_ref make() noexcept {
    return address_space_ref(Tag{});
  }

  /**
   * @brief Creates a handle for a stateful address-space tag.
   * @tparam Tag Address-space tag type.
   * @tparam Context Concrete context type.
   * @tparam Traits Specialized traits, enabled when `context_type` is non-void.
   * @param ctx Context object providing the reads.
   * @return An @ref address_space_ref bound to @p ctx.
   */
  template <
      typename Tag, typename Context,
      typename Traits = address_space_traits<Tag>,
      std::enable_if_t<!std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr address_space_ref
  make(const Context &ctx) noexcept {
    return address_space_ref(Tag{}, ctx);
  }

  /**
   * @brief Reads raw bytes from the remote address space.
   * @param addr Absolute source address.
   * @param dest Destination buffer.
   * @param size Number of bytes to read.
   * @return `true` on success, `false` if the handle is empty or the read
   * fails.
   */
  [[nodiscard]] bool read_bytes(uintptr_t addr, void *dest,
                                size_t size) const noexcept {
    if (!vtbl_)
      return false;
    return vtbl_->read_bytes(ctx_, addr, dest, size);
  }

  /**
   * @brief Reads a trivially-copyable object from the remote address space.
   * @tparam T Object type (must be trivially copyable).
   * @param addr Absolute address of the object.
   * @param out_obj Receives the loaded object.
   * @return `true` on success, `false` otherwise.
   */
  template <typename T>
  [[nodiscard]] bool read(uintptr_t addr, T &out_obj) const noexcept {
    static_assert(std::is_trivially_copyable_v<T>,
                  "Target type T must be trivially copyable");
    return read_bytes(addr, &out_obj, sizeof(T));
  }

  /**
   * @brief Reads a bounded chunk of a remote NUL-terminated string.
   * @param addr Absolute source address.
   * @param buffer Scratch buffer receiving the chunk.
   * @param out_len Receives the number of characters read.
   * @param null_term Receives whether a terminator was found.
   * @return `true` on success, `false` otherwise.
   */
  [[nodiscard]] bool read_string_chunk(uintptr_t addr, span<char> buffer,
                                       size_t &out_len,
                                       bool &null_term) const noexcept {
    if (!vtbl_ || buffer.empty())
      return false;
    return vtbl_->read_string(ctx_, addr, buffer.data(), buffer.size(), out_len,
                              null_term);
  }

  /**
   * @brief Reports whether the handle is bound to an address space.
   * @return `true` when the handle is valid.
   */
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return vtbl_ != nullptr;
  }

private:
  template <typename Tag>
  static constexpr vtable s_vtbl{&address_space_traits<Tag>::read_bytes,
                                 &address_space_traits<Tag>::read_string};

  const void *ctx_{nullptr};
  const vtable *vtbl_{nullptr};
};

// ============================================================================
// Compact Views
// ============================================================================

/**
 * @brief Streaming view of a remote NUL-terminated string.
 *
 * Pulls the string through a reusable scratch buffer in bounded chunks to keep
 * stack usage minimal.
 */
class remote_string_view {
public:
  /**
   * @brief Constructs an empty (null) view.
   */
  constexpr remote_string_view() noexcept = default;

  /**
   * @brief Constructs a view over a remote string.
   * @param addr Absolute address of the string.
   * @param space Address space the string lives in.
   * @param scratch Reusable chunk-read buffer.
   * @param max_limit Maximum characters to render.
   */
  constexpr remote_string_view(uintptr_t addr, address_space_ref space,
                               span<char> scratch,
                               size_t max_limit = 4096) noexcept
      : addr_(addr), space_(space), scratch_(scratch), max_limit_(max_limit) {}

  /**
   * @brief Constructs a view over a remote string using a fixed C array as
   * scratch.
   * @tparam N Size of the scratch array.
   * @param addr Absolute address of the string.
   * @param space Address space the string lives in.
   * @param scratch Reusable chunk-read buffer.
   * @param max_limit Maximum characters to render.
   */
  template <size_t N>
  constexpr remote_string_view(uintptr_t addr, address_space_ref space,
                               char (&scratch)[N],
                               size_t max_limit = 4096) noexcept
      : addr_(addr), space_(space), scratch_(scratch, N),
        max_limit_(max_limit) {}

  /**
   * @brief Returns the remote string address.
   * @return Absolute address, or `0` when null.
   */
  [[nodiscard]] constexpr uintptr_t address() const noexcept { return addr_; }
  /**
   * @brief Returns the address space handle.
   * @return Bound @ref address_space_ref.
   */
  [[nodiscard]] constexpr address_space_ref space() const noexcept {
    return space_;
  }
  /**
   * @brief Returns the scratch chunk buffer.
   * @return Scratch span used for chunked reads.
   */
  [[nodiscard]] constexpr span<char> scratch() const noexcept {
    return scratch_;
  }
  /**
   * @brief Returns the maximum render length.
   * @return Character limit.
   */
  [[nodiscard]] constexpr size_t max_limit() const noexcept {
    return max_limit_;
  }

private:
  /// Remote string address.
  uintptr_t addr_{0};
  /// Address space handle.
  address_space_ref space_{};
  /// Scratch chunk buffer.
  span<char> scratch_{};
  /// Maximum render length.
  size_t max_limit_{4096};
};

/**
 * @brief Lazily-loaded reference to a trivially-copyable remote object.
 *
 * @tparam T Object type loaded into a caller-supplied scratch buffer.
 */
template <typename T> class remote_ref {
public:
  /**
   * @brief Constructs a remote object reference.
   * @param addr Absolute address of the object.
   * @param space Address space the object lives in.
   * @param scratch Reusable, properly-aligned scratch buffer (`>= sizeof(T)`).
   */
  constexpr remote_ref(uintptr_t addr, address_space_ref space,
                       span<std::byte> scratch) noexcept
      : addr_(addr), space_(space), scratch_(scratch) {}

  /**
   * @brief Constructs a remote object reference over a fixed C array.
   * @tparam N Size of the scratch array.
   * @param addr Absolute address of the object.
   * @param space Address space the object lives in.
   * @param scratch Reusable, properly-aligned scratch buffer.
   */
  template <size_t N>
  constexpr remote_ref(uintptr_t addr, address_space_ref space,
                       std::byte (&scratch)[N]) noexcept
      : addr_(addr), space_(space), scratch_(scratch, N) {}

  /**
   * @brief Returns the remote object address.
   * @return Absolute address, or `0` when null.
   */
  [[nodiscard]] constexpr uintptr_t address() const noexcept { return addr_; }
  /**
   * @brief Reports whether the reference points to address zero.
   * @return `true` when the reference is null.
   */
  [[nodiscard]] constexpr bool is_null() const noexcept { return addr_ == 0; }

  /**
   * @brief Loads the object into the scratch buffer.
   * @param out_ptr Receives a pointer into the scratch buffer holding the
   * loaded object.
   * @return `true` on success, `false` if scratch is too small, misaligned, or
   * the read fails.
   */
  [[nodiscard]] bool load(T *&out_ptr) const noexcept {
    if (scratch_.size() < sizeof(T))
      return false;
    if (reinterpret_cast<uintptr_t>(scratch_.data()) % alignof(T) != 0)
      return false;

    out_ptr = reinterpret_cast<T *>(scratch_.data());
    return space_.read_bytes(addr_, out_ptr, sizeof(T));
  }

private:
  /// Remote object address.
  uintptr_t addr_{0};
  /// Address space handle.
  address_space_ref space_{};
  /// Scratch buffer for the staged object.
  span<std::byte> scratch_{};
};

// ============================================================================
// microfmt Formatters
// ============================================================================

/**
 * @brief Formatter rendering a @ref remote_string_view.
 *
 * Emits `(null)` for the null view and `<invalid-space@0x..>` /
 * `<fault@0x..>` markers for read failures.
 */
template <> struct formatter<remote_string_view> {
  /**
   * @brief No-op parse; remote strings accept no format specifier.
   * @param ctx Unused format parse context.
   */
  constexpr void parse(format_parse_context &) noexcept {}

  /**
   * @brief Renders the remote string, chunk-by-chunk.
   * @param view The remote string view to format.
   * @param out Destination sink.
   */
  void format(const remote_string_view &view, const sink &out) const noexcept {
    if (view.address() == 0) {
      out.write("(null)");
      return;
    }

    if (!view.space() || view.scratch().empty()) {
      microfmt::format_to(out, "<invalid-space@{:#x}>", view.address());
      return;
    }

    uintptr_t cur = view.address();
    size_t total = 0;

    while (total < view.max_limit()) {
      size_t chunk_len = 0;
      bool null_term = false;

      if (!view.space().read_string_chunk(cur, view.scratch(), chunk_len,
                                          null_term)) {
        if (total == 0) {
          microfmt::format_to(out, "<fault@{:#x}>", view.address());
        } else {
          out.write("<fault>");
        }
        return;
      }

      if (chunk_len > 0) {
        size_t limit_left = view.max_limit() - total;
        size_t to_write = (chunk_len > limit_left) ? limit_left : chunk_len;
        out.write(std::string_view(view.scratch().data(), to_write));
        total += to_write;
        cur += to_write;
      }

      if (null_term || total >= view.max_limit()) {
        break;
      }
    }

    if (total >= view.max_limit()) {
      out.write("...");
    }
  }
};

/**
 * @brief Formatter that lazily loads and renders a remote object via
 * @ref remote_ref.
 * @tparam T Referenced remote object type.
 */
template <typename T> struct formatter<remote_ref<T>> {
  /**
   * @brief Specifier forwarded to the loaded object's formatter.
   */
  std::string_view spec_{""};

  /**
   * @brief Captures the specifier for the element formatter.
   * @param ctx Format parse context exposing the specifier text.
   */
  constexpr void parse(format_parse_context &ctx) noexcept {
    spec_ = ctx.spec();
  }

  /**
   * @brief Loads and renders the remote object.
   * @param view The remote object reference to format.
   * @param out Destination sink.
   */
  void format(const remote_ref<T> &view, const sink &out) const noexcept {
    if (view.is_null()) {
      out.write("(null)");
      return;
    }

    T *staged = nullptr;
    if (!view.load(staged)) {
      microfmt::format_to(out, "<fault@{:#x}>", view.address());
      return;
    }

    formatter<T> elem_fmt;
    format_parse_context pctx(spec_);
    elem_fmt.parse(pctx);
    elem_fmt.format(*staged, out);
  }
};

} // namespace microfmt
