// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file remote_vector.hpp
 * @brief Traits-based formatting support for remote contiguous sequences. */

#include "remote_container.hpp"
#include "remote_layout_accessor.hpp"
#include "remote_object.hpp"

namespace microfmt {

/**
 * @brief Static customization point for a remote vector implementation.
 *
 * Specializations declare `context_type` and provide `get_size`,
 * `get_element_address`, and `format_element`.
 */
template <typename Tag> struct remote_vector_traits;

namespace detail {

template <typename Tag> struct remote_vector_dispatch {
  using traits_type = remote_vector_traits<Tag>;
  using context_type = typename traits_type::context_type;

  static bool format(value_ref<const context_type> context, uintptr_t container_addr,
                     const container_options &opts, address_space_ref space,
                     span<std::byte> scratch, const sink &out) noexcept {
    size_t size = 0;
    if (!traits_type::get_size(context, container_addr, space, scratch, size)) {
      out.write(opts.open_bracket);
      out.write(opts.close_bracket);
      return true;
    }

    out.write(opts.open_bracket);
    const size_t print_count = size < opts.max_print ? size : opts.max_print;
    for (size_t index = 0; index < print_count; ++index) {
      if (index > 0)
        out.write(opts.entry_separator);

      uintptr_t element_addr = 0;
      if (!traits_type::get_element_address(
              context, container_addr, space, scratch, index, element_addr) ||
          !traits_type::format_element(context, space, scratch, element_addr,
                                       out)) {
        out.write("<fault>");
        break;
      }
    }

    if (size > opts.max_print) {
      out.write(opts.entry_separator);
      out.write("...");
    }
    out.write(opts.close_bracket);
    return true;
  }
};

template <typename T, typename RemotePtr, typename RemoteSize>
struct vector_layout_tag {};
template <typename T> struct carray_layout_tag {};

template <typename T>
bool format_remote_vector_element(address_space_ref space,
                                  span<std::byte> scratch,
                                  uintptr_t element_addr,
                                  const sink &out) noexcept {
  if constexpr (remote_object_traits<T>::is_registered) {
    remote_object_view view(element_addr, space, type_tag<T>{}, scratch);
    formatter<remote_object_view>().format(view, out);
  } else {
    scratch_allocator allocator(scratch);
    T *value = allocator.allocate<T>();
    if (!value || !space.read_bytes(element_addr, value, sizeof(T)))
      return false;
    formatter<T>().format(*value, out);
  }
  return true;
}

} // namespace detail

template <typename Tag>
using remote_vector =
    basic_remote_container<Tag, detail::remote_vector_dispatch<Tag>>;

template <typename Tag>
[[nodiscard]] constexpr remote_vector<Tag>
make_remote_vector(
    uintptr_t container_addr,
    typename remote_vector_traits<Tag>::context_type context) noexcept {
  return remote_vector<Tag>(container_addr, std::move(context));
}

template <typename T, typename RemotePtr, typename RemoteSize>
struct remote_vector_traits<
    detail::vector_layout_tag<T, RemotePtr, RemoteSize>> {
  using pointer_query =
      decltype(make_remote_offset_query<uintptr_t, RemotePtr>(0));
  using size_query = decltype(make_remote_offset_query<size_t, RemoteSize>(0));

  struct context_type {
    pointer_query data;
    size_query size;
    size_query capacity;
    bool has_capacity;
  };

  static bool get_size(value_ref<const context_type> context,
                       uintptr_t container_addr, address_space_ref space,
                       span<std::byte>, size_t &out_size) noexcept {
    return context->size(space, container_addr, out_size);
  }

  static bool get_capacity(value_ref<const context_type> context,
                           uintptr_t container_addr,
                           address_space_ref space, span<std::byte>,
                           size_t &out_capacity) noexcept {
    return context->has_capacity &&
           context->capacity(space, container_addr, out_capacity);
  }

  static bool get_element_address(value_ref<const context_type> context,
                                  uintptr_t container_addr,
                                  address_space_ref space, span<std::byte>,
                                  size_t index,
                                  uintptr_t &out_element_addr) noexcept {
    uintptr_t data_address = 0;
    if (!context->data(space, container_addr, data_address))
      return false;
    out_element_addr = data_address + index * sizeof(T);
    return true;
  }

  static bool format_element(value_ref<const context_type>, address_space_ref space,
                             span<std::byte> scratch,
                             uintptr_t element_addr,
                             const sink &out) noexcept {
    return detail::format_remote_vector_element<T>(space, scratch,
                                                   element_addr, out);
  }
};

template <typename T>
struct remote_vector_traits<detail::carray_layout_tag<T>> {
  struct context_type {
    size_t count;
  };

  static bool get_size(value_ref<const context_type> context, uintptr_t,
                       address_space_ref, span<std::byte>,
                       size_t &out_size) noexcept {
    out_size = context->count;
    return true;
  }

  static bool get_element_address(value_ref<const context_type> context,
                                  uintptr_t container_addr,
                                  address_space_ref, span<std::byte>,
                                  size_t index,
                                  uintptr_t &out_element_addr) noexcept {
    if (index >= context->count)
      return false;
    out_element_addr = container_addr + index * sizeof(T);
    return true;
  }

  static bool format_element(value_ref<const context_type>, address_space_ref space,
                             span<std::byte> scratch,
                             uintptr_t element_addr,
                             const sink &out) noexcept {
    return detail::format_remote_vector_element<T>(space, scratch,
                                                   element_addr, out);
  }
};

/**
 * @brief Creates a vector inspector for a conventional offset layout.
 */
template <typename T, typename RemotePtr = uintptr_t,
          typename RemoteSize = size_t>
[[nodiscard]] constexpr auto
make_remote_vector(uintptr_t container_addr, ptrdiff_t data_offset,
                   ptrdiff_t size_offset,
                   ptrdiff_t capacity_offset = -1) noexcept {
  using tag = detail::vector_layout_tag<T, RemotePtr, RemoteSize>;
  using traits = remote_vector_traits<tag>;
  typename traits::context_type context{
      make_remote_offset_query<uintptr_t, RemotePtr>(data_offset),
      make_remote_offset_query<size_t, RemoteSize>(size_offset),
      make_remote_offset_query<size_t, RemoteSize>(capacity_offset),
      capacity_offset >= 0};
  return remote_vector<tag>(container_addr, std::move(context));
}

/**
 * @brief Creates a vector inspector for a fixed-size remote C array.
 */
template <typename T>
[[nodiscard]] constexpr auto make_remote_carray(uintptr_t array_addr,
                                                 size_t count) noexcept {
  using tag = detail::carray_layout_tag<T>;
  return remote_vector<tag>(
      array_addr, typename remote_vector_traits<tag>::context_type{count});
}

} // namespace microfmt
