#pragma once

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

template <typename Tag> struct address_space_traits;

// Built-in Local Address Space
struct local_space_tag {};

template <> struct address_space_traits<local_space_tag> {
  using context_type = void;

  static bool read_bytes(const void *, uintptr_t addr, void *dest,
                         size_t size) noexcept {
    if (addr == 0)
      return false;
    std::memcpy(dest, reinterpret_cast<const void *>(addr), size);
    return true;
  }

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

class address_space_ref {
public:
  struct vtable {
    bool (*read_bytes)(const void *ctx, uintptr_t addr, void *dest,
                       size_t size) noexcept;
    bool (*read_string)(const void *ctx, uintptr_t addr, char *dest,
                        size_t max_len, size_t &out_len,
                        bool &null_term) noexcept;
  };

  constexpr address_space_ref() noexcept = default;

  // Stateless constructor
  template <
      typename Tag, typename Traits = address_space_traits<Tag>,
      std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  constexpr explicit address_space_ref(Tag) noexcept
      : ctx_(nullptr), vtbl_(&s_vtbl<Tag>) {}

  // Stateful constructor
  template <typename Tag, typename Context,
            typename Traits = address_space_traits<Tag>,
            std::enable_if_t<
                !std::is_void_v<typename Traits::context_type> &&
                    std::is_convertible_v<
                        const Context *, const typename Traits::context_type *>,
                int> = 0>
  constexpr address_space_ref(Tag, const Context &ctx) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag>) {}

  template <
      typename Tag, typename Traits = address_space_traits<Tag>,
      std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr address_space_ref make() noexcept {
    return address_space_ref(Tag{});
  }

  template <
      typename Tag, typename Context,
      typename Traits = address_space_traits<Tag>,
      std::enable_if_t<!std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr address_space_ref
  make(const Context &ctx) noexcept {
    return address_space_ref(Tag{}, ctx);
  }

  [[nodiscard]] bool read_bytes(uintptr_t addr, void *dest,
                                size_t size) const noexcept {
    if (!vtbl_)
      return false;
    return vtbl_->read_bytes(ctx_, addr, dest, size);
  }

  template <typename T>
  [[nodiscard]] bool read(uintptr_t addr, T &out_obj) const noexcept {
    static_assert(std::is_trivially_copyable_v<T>,
                  "Target type T must be trivially copyable");
    return read_bytes(addr, &out_obj, sizeof(T));
  }

  [[nodiscard]] bool read_string_chunk(uintptr_t addr, span<char> buffer,
                                       size_t &out_len,
                                       bool &null_term) const noexcept {
    if (!vtbl_ || buffer.empty())
      return false;
    return vtbl_->read_string(ctx_, addr, buffer.data(), buffer.size(), out_len,
                              null_term);
  }

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

class remote_string_view {
public:
  constexpr remote_string_view() noexcept = default;

  constexpr remote_string_view(uintptr_t addr, address_space_ref space,
                               span<char> scratch,
                               size_t max_limit = 4096) noexcept
      : addr_(addr), space_(space), scratch_(scratch), max_limit_(max_limit) {}

  template <size_t N>
  constexpr remote_string_view(uintptr_t addr, address_space_ref space,
                               char (&scratch)[N],
                               size_t max_limit = 4096) noexcept
      : addr_(addr), space_(space), scratch_(scratch, N),
        max_limit_(max_limit) {}

  [[nodiscard]] constexpr uintptr_t address() const noexcept { return addr_; }
  [[nodiscard]] constexpr address_space_ref space() const noexcept {
    return space_;
  }
  [[nodiscard]] constexpr span<char> scratch() const noexcept {
    return scratch_;
  }
  [[nodiscard]] constexpr size_t max_limit() const noexcept {
    return max_limit_;
  }

private:
  uintptr_t addr_{0};
  address_space_ref space_{};
  span<char> scratch_{};
  size_t max_limit_{4096};
};

template <typename T> class remote_ref {
public:
  constexpr remote_ref(uintptr_t addr, address_space_ref space,
                       span<std::byte> scratch) noexcept
      : addr_(addr), space_(space), scratch_(scratch) {}

  template <size_t N>
  constexpr remote_ref(uintptr_t addr, address_space_ref space,
                       std::byte (&scratch)[N]) noexcept
      : addr_(addr), space_(space), scratch_(scratch, N) {}

  [[nodiscard]] constexpr uintptr_t address() const noexcept { return addr_; }
  [[nodiscard]] constexpr bool is_null() const noexcept { return addr_ == 0; }

  [[nodiscard]] bool load(T *&out_ptr) const noexcept {
    if (scratch_.size() < sizeof(T))
      return false;
    if (reinterpret_cast<uintptr_t>(scratch_.data()) % alignof(T) != 0)
      return false;

    out_ptr = reinterpret_cast<T *>(scratch_.data());
    return space_.read_bytes(addr_, out_ptr, sizeof(T));
  }

private:
  uintptr_t addr_{0};
  address_space_ref space_{};
  span<std::byte> scratch_{};
};

// ============================================================================
// microfmt Formatters
// ============================================================================

template <> struct formatter<remote_string_view> {
  constexpr void parse(format_parse_context &) noexcept {}

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

template <typename T> struct formatter<remote_ref<T>> {
  std::string_view spec_{""};

  constexpr void parse(format_parse_context &ctx) noexcept {
    spec_ = ctx.spec();
  }

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
