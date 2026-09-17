// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file remote_basic_string.hpp
 * @brief Bounded views over remote C++ string objects. */

#include "address_space.hpp"
#include "remote_layout_accessor.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace microfmt {

namespace detail {

template <typename SizeQuery, typename DataQuery>
struct basic_string_query_layout {
  SizeQuery size_query;
  DataQuery data_query;

  [[nodiscard]] bool read_size(address_space_ref space,
                               uintptr_t object_address,
                               size_t &size) const noexcept {
    return size_query(space, object_address, size);
  }

  [[nodiscard]] bool
  read_data_address(address_space_ref space, uintptr_t object_address,
                    size_t size, uintptr_t &data_address) const noexcept {
    return data_query(space, object_address, data_address, size);
  }
};

template <typename DataQuery> struct size_selected_data_query {
  DataQuery allocated_data_query;
  ptrdiff_t inline_data_offset;
  size_t inline_capacity;

  [[nodiscard]] bool operator()(address_space_ref space,
                                uintptr_t object_address,
                                uintptr_t &data_address,
                                size_t size) const noexcept {
    if (size <= inline_capacity) {
      data_address = add_address_offset(object_address, inline_data_offset);
      return true;
    }
    return allocated_data_query(space, object_address, data_address);
  }
};

} // namespace detail

/**
 * @brief Length-aware view over a remote C++ string object.
 *
 * The layout object resolves the character-data address and byte length from
 * the remote string representation. Formatting reads exactly that many bytes
 * in bounded chunks, so embedded NUL characters do not terminate the read.
 *
 * @tparam Layout Object providing separate `read_size` and
 * `read_data_address` operations.
 */
template <typename Layout> class MICROFMT_POINTER remote_basic_string_view {
public:
  /**
   * @brief Constructs a view over a remote string object.
   * @param object_address Address of the remote string object.
   * @param space Address space containing the object and character data.
   * @param scratch Reusable character-read buffer.
   * @param layout Layout resolver retained by value.
   * @param max_limit Maximum characters to render.
   */
  constexpr remote_basic_string_view(
      uintptr_t object_address, address_space_ref space,
      span<char> scratch MICROFMT_LIFETIMEBOUND MICROFMT_LIFETIME_CAPTURE_BY_THIS,
      Layout layout, size_t max_limit = 4096) noexcept
      : object_address_(object_address), space_(space), scratch_(scratch),
        layout_(layout), max_limit_(max_limit) {}

  /**
   * @brief Constructs a view using a fixed C array as scratch storage.
   */
  template <size_t N>
  constexpr remote_basic_string_view(
      uintptr_t object_address, address_space_ref space,
      char (&scratch MICROFMT_LIFETIMEBOUND MICROFMT_LIFETIME_CAPTURE_BY_THIS)[N],
      Layout layout, size_t max_limit = 4096) noexcept
      : object_address_(object_address), space_(space), scratch_(scratch, N),
        layout_(layout), max_limit_(max_limit) {}

  [[nodiscard]] constexpr uintptr_t object_address() const noexcept {
    return object_address_;
  }

  [[nodiscard]] constexpr address_space_ref space() const noexcept {
    return space_;
  }

  [[nodiscard]] constexpr span<char>
  scratch() const noexcept MICROFMT_LIFETIMEBOUND {
    return scratch_;
  }

  [[nodiscard]] constexpr size_t max_limit() const noexcept {
    return max_limit_;
  }

  [[nodiscard]] bool resolve(uintptr_t &data_address,
                             size_t &size) const noexcept {
    return layout_.read_size(space_, object_address_, size) &&
           layout_.read_data_address(space_, object_address_, size,
                                     data_address);
  }

private:
  uintptr_t object_address_{0};
  address_space_ref space_{};
  span<char> scratch_{};
  Layout layout_;
  size_t max_limit_{4096};
};

/**
 * @brief Creates a length-aware remote string view with template deduction.
 */
template <typename Layout>
[[nodiscard]] constexpr auto make_remote_basic_string_view(
    uintptr_t object_address, address_space_ref space, span<char> scratch,
    Layout layout, size_t max_limit = 4096) noexcept {
  return remote_basic_string_view<Layout>(object_address, space, scratch,
                                          layout, max_limit);
}

template <typename Layout, size_t N>
[[nodiscard]] constexpr auto make_remote_basic_string_view(
    uintptr_t object_address, address_space_ref space, char (&scratch)[N],
    Layout layout, size_t max_limit = 4096) noexcept {
  return remote_basic_string_view<Layout>(object_address, space, scratch,
                                          layout, max_limit);
}

/**
 * @brief Factories for common remote C++ string object layouts.
 */
struct remote_basic_string_traits {
  /**
   * @brief Creates a layout from explicit size and data-address readers.
   * @param size_reader Callback retained by the size query.
   * @param data_reader Callback retained by the data-address query. It receives
   * the decoded size after the output reference.
   */
  template <typename SizeReader, typename DataReader>
  [[nodiscard]] static constexpr auto
  callback_layout(SizeReader size_reader, DataReader data_reader) noexcept {
    auto size_query = make_remote_layout_query<size_t>(size_reader);
    auto data_query = make_remote_layout_query<uintptr_t>(data_reader);
    return detail::basic_string_query_layout<decltype(size_query),
                                             decltype(data_query)>{
        size_query, data_query};
  }

  /**
   * @brief Creates a layout whose object always stores a data pointer and size.
   *
   * This also supports SSO implementations, such as libstdc++, whose data
   * pointer points at the object's inline buffer while the string is short.
   */
  template <typename RemotePtr = uintptr_t, typename RemoteSize = size_t>
  [[nodiscard]] static constexpr auto
  pointer_size_layout(ptrdiff_t data_offset,
                      ptrdiff_t size_offset) noexcept {
    auto size_query =
        make_remote_offset_query<size_t, RemoteSize>(size_offset);
    auto data_query =
        make_remote_offset_query<uintptr_t, RemotePtr>(data_offset);
    return detail::basic_string_query_layout<decltype(size_query),
                                             decltype(data_query)>{
        size_query, data_query};
  }

  /**
   * @brief Creates a layout selected by the decoded string length.
   *
   * Strings no longer than @p inline_capacity are read from storage embedded
   * in the object. Longer strings use the pointer stored at @p data_offset.
   */
  template <typename RemotePtr = uintptr_t, typename RemoteSize = size_t>
  [[nodiscard]] static constexpr auto
  size_selected_layout(ptrdiff_t data_offset, ptrdiff_t size_offset,
                       ptrdiff_t inline_data_offset,
                       size_t inline_capacity) noexcept {
    auto size_query =
        make_remote_offset_query<size_t, RemoteSize>(size_offset);
    auto allocated_data_query =
        make_remote_offset_query<uintptr_t, RemotePtr>(data_offset);
    auto data_reader =
        detail::size_selected_data_query<decltype(allocated_data_query)>{
            allocated_data_query, inline_data_offset, inline_capacity};
    auto data_query = make_remote_layout_query<uintptr_t>(data_reader);
    return detail::basic_string_query_layout<decltype(size_query),
                                             decltype(data_query)>{
        size_query, data_query};
  }
};

template <typename Layout>
struct formatter<remote_basic_string_view<Layout>> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const remote_basic_string_view<Layout> &view,
              const sink &out) const noexcept {
    if (view.object_address() == 0) {
      out.write("(null)");
      return;
    }
    if (!view.space()) {
      microfmt::format_to(out, MICROFMT_STRING("<invalid-space@{:#x}>"),
                          view.object_address());
      return;
    }

    uintptr_t data_address = 0;
    size_t size = 0;
    if (!view.resolve(data_address, size)) {
      microfmt::format_to(out, MICROFMT_STRING("<fault@{:#x}>"),
                          view.object_address());
      return;
    }
    if (size == 0)
      return;
    if (data_address == 0) {
      microfmt::format_to(out, MICROFMT_STRING("<fault@{:#x}>"),
                          view.object_address());
      return;
    }

    const size_t render_size =
        size < view.max_limit() ? size : view.max_limit();
    const auto scratch = view.scratch();
    if (render_size != 0 && scratch.empty()) {
      microfmt::format_to(out, MICROFMT_STRING("<invalid-space@{:#x}>"),
                          view.object_address());
      return;
    }

    size_t total = 0;
    while (total < render_size) {
      const size_t remaining = render_size - total;
      const size_t chunk_size =
          remaining < scratch.size() ? remaining : scratch.size();
      if (data_address > std::numeric_limits<uintptr_t>::max() - total ||
          !view.space().read_bytes(data_address + total, scratch.data(),
                                   chunk_size)) {
        if (total == 0) {
          microfmt::format_to(out, MICROFMT_STRING("<fault@{:#x}>"),
                              data_address);
        } else {
          out.write("<fault>");
        }
        return;
      }
      out.write(microfmt::string_view(scratch.data(), chunk_size));
      total += chunk_size;
    }

    if (size > view.max_limit())
      out.write("...");
  }
};

} // namespace microfmt
