// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file remote_forward_list.hpp
 * @brief Traits-based formatting support for remote singly-linked lists. */

#include "remote_container.hpp"
#include "remote_layout_accessor.hpp"
#include "remote_object.hpp"

namespace microfmt {

/**
 * @brief Static customization point for a remote forward-list implementation.
 *
 * Specializations declare `context_type` and provide `get_head_node`,
 * `get_next_node`, and `format_node_element`. Each operation receives a
 * required mutable context borrow so implementations can retain traversal
 * statistics or caches without a runtime function table.
 */
template <typename Tag> struct remote_forward_list_traits;

namespace detail {

template <typename Tag> struct remote_forward_list_dispatch {
  using traits_type = remote_forward_list_traits<Tag>;
  using context_type = typename traits_type::context_type;

  static bool format(value_ref<const context_type> context, uintptr_t container_addr,
                     const container_options &opts, address_space_ref space,
                     span<std::byte> scratch, const sink &out) noexcept {
    out.write(opts.open_bracket);

    uintptr_t current_node = 0;
    if (!traits_type::get_head_node(context, container_addr, space, scratch,
                                    current_node)) {
      out.write(opts.close_bracket);
      return true;
    }

    size_t print_count = 0;
    while (current_node != 0) {
      if (print_count >= opts.max_print) {
        if (print_count > 0)
          out.write(opts.entry_separator);
        out.write("...");
        break;
      }
      if (print_count > 0)
        out.write(opts.entry_separator);

      if (!traits_type::format_node_element(context, space, scratch,
                                            current_node, out)) {
        out.write("<fault>");
        break;
      }

      uintptr_t next_node = 0;
      if (!traits_type::get_next_node(context, space, scratch, current_node,
                                      next_node)) {
        out.write("<fault>");
        break;
      }
      current_node = next_node;
      ++print_count;
    }

    out.write(opts.close_bracket);
    return true;
  }
};

template <typename T, typename RemotePtr> struct forward_list_layout_tag {};

} // namespace detail

template <typename Tag>
using remote_forward_list =
    basic_remote_container<Tag, detail::remote_forward_list_dispatch<Tag>>;

/**
 * @brief Creates a typed remote forward list with caller-defined traits.
 */
template <typename Tag>
[[nodiscard]] constexpr remote_forward_list<Tag>
make_remote_forward_list(
    uintptr_t container_addr,
    typename remote_forward_list_traits<Tag>::context_type context) noexcept {
  return remote_forward_list<Tag>(container_addr, std::move(context));
}

template <typename T, typename RemotePtr>
struct remote_forward_list_traits<
    detail::forward_list_layout_tag<T, RemotePtr>> {
  using pointer_query =
      decltype(make_remote_offset_query<uintptr_t, RemotePtr>(0));

  struct context_type {
    pointer_query head;
    pointer_query next;
    ptrdiff_t data_offset;
  };

  static bool get_head_node(value_ref<const context_type> context,
                            uintptr_t container_addr,
                            address_space_ref space, span<std::byte>,
                            uintptr_t &out_node_addr) noexcept {
    return context->head(space, container_addr, out_node_addr);
  }

  static bool get_next_node(value_ref<const context_type> context,
                            address_space_ref space, span<std::byte>,
                            uintptr_t node_addr,
                            uintptr_t &out_next_addr) noexcept {
    return context->next(space, node_addr, out_next_addr);
  }

  static bool format_node_element(value_ref<const context_type> context,
                                  address_space_ref space,
                                  span<std::byte> scratch,
                                  uintptr_t node_addr,
                                  const sink &out) noexcept {
    const uintptr_t elem_addr =
        detail::add_address_offset(node_addr, context->data_offset);
    if constexpr (remote_object_traits<T>::is_registered) {
      remote_object_view obj_view(elem_addr, space, type_tag<T>{}, scratch);
      formatter<remote_object_view>().format(obj_view, out);
    } else {
      scratch_allocator allocator(scratch);
      T *value = allocator.allocate<T>();
      if (!value || !space.read_bytes(elem_addr, value, sizeof(T)))
        return false;
      formatter<T>().format(*value, out);
    }
    return true;
  }
};

/**
 * @brief Creates a forward-list inspector for a conventional offset layout.
 */
template <typename T, typename RemotePtr = uintptr_t>
[[nodiscard]] constexpr auto
make_remote_forward_list(uintptr_t container_addr, ptrdiff_t head_offset,
                         ptrdiff_t next_offset,
                         ptrdiff_t data_offset) noexcept {
  using tag = detail::forward_list_layout_tag<T, RemotePtr>;
  using traits = remote_forward_list_traits<tag>;
  typename traits::context_type context{
      make_remote_offset_query<uintptr_t, RemotePtr>(head_offset),
      make_remote_offset_query<uintptr_t, RemotePtr>(next_offset),
      data_offset};
  return remote_forward_list<tag>(container_addr, std::move(context));
}

} // namespace microfmt
