// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file remote_binary_tree.hpp
 * @brief Traits-based formatting support for remote binary trees. */

#include "remote_container.hpp"
#include "remote_layout_accessor.hpp"
#include "remote_object.hpp"

#include <algorithm>

namespace microfmt {

/**
 * @brief Static customization point for a remote binary tree.
 *
 * Specializations declare `context_type` and provide `get_root_node`,
 * `get_left_node`, `get_right_node`, and `format_node`.
 */
template <typename Tag> struct remote_binary_tree_traits;

namespace detail {

template <typename Tag> struct remote_binary_tree_dispatch {
  using traits_type = remote_binary_tree_traits<Tag>;
  using context_type = typename traits_type::context_type;

  static bool format(value_ref<const context_type> context, uintptr_t container_addr,
                     const container_options &opts, address_space_ref space,
                     span<std::byte> scratch, const sink &out) noexcept {
    out.write(opts.open_bracket);
    if (scratch.size() < sizeof(uintptr_t) * 8) {
      out.write("<fault>");
      out.write(opts.close_bracket);
      return true;
    }

    constexpr size_t max_tree_stack_depth = 64;
    const size_t available_elements = scratch.size() / sizeof(uintptr_t);
    const size_t stack_capacity =
        std::min(available_elements / 2, max_tree_stack_depth);
    scratch_allocator allocator(scratch);
    uintptr_t *node_stack = allocator.allocate<uintptr_t>(stack_capacity);
    if (!node_stack) {
      out.write("<fault>");
      out.write(opts.close_bracket);
      return true;
    }
    span<std::byte> element_scratch = allocator.remaining_span();

    uintptr_t root_node = 0;
    if (!traits_type::get_root_node(context, container_addr, space,
                                    element_scratch, root_node) ||
        root_node == 0) {
      out.write(opts.close_bracket);
      return true;
    }

    size_t stack_top = 0;
    uintptr_t current = root_node;
    size_t print_count = 0;
    while (current != 0 || stack_top > 0) {
      while (current != 0) {
        if (stack_top >= stack_capacity)
          break;
        MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;
        node_stack[stack_top++] = current;
        MICROFMT_END_UNSAFE_BUFFER_USAGE;

        uintptr_t left = 0;
        if (!traits_type::get_left_node(context, space, element_scratch,
                                        current, left))
          break;
        current = left;
      }
      if (stack_top == 0)
        break;

      MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;
      current = node_stack[--stack_top];
      MICROFMT_END_UNSAFE_BUFFER_USAGE;

      if (print_count >= opts.max_print) {
        if (print_count > 0)
          out.write(opts.entry_separator);
        out.write("...");
        break;
      }
      if (print_count > 0)
        out.write(opts.entry_separator);
      if (!traits_type::format_node(context, space, element_scratch, current,
                                    opts, out)) {
        out.write("<fault>");
        break;
      }
      ++print_count;

      uintptr_t right = 0;
      current = traits_type::get_right_node(context, space, element_scratch,
                                            current, right)
                    ? right
                    : 0;
    }

    out.write(opts.close_bracket);
    return true;
  }
};

template <typename Key, typename Value, typename RemotePtr>
struct binary_tree_layout_tag {};

} // namespace detail

template <typename Tag>
using remote_binary_tree =
    basic_remote_container<Tag, detail::remote_binary_tree_dispatch<Tag>>;

template <typename Tag>
[[nodiscard]] constexpr remote_binary_tree<Tag>
make_remote_binary_tree(
    uintptr_t container_addr,
    typename remote_binary_tree_traits<Tag>::context_type context) noexcept {
  return remote_binary_tree<Tag>(container_addr, std::move(context));
}

template <typename Key, typename Value, typename RemotePtr>
struct remote_binary_tree_traits<
    detail::binary_tree_layout_tag<Key, Value, RemotePtr>> {
  using pointer_query =
      decltype(make_remote_offset_query<uintptr_t, RemotePtr>(0));

  struct context_type {
    pointer_query root;
    pointer_query left;
    pointer_query right;
    ptrdiff_t key_offset;
    ptrdiff_t value_offset;
  };

  static bool get_root_node(value_ref<const context_type> context,
                            uintptr_t container_addr,
                            address_space_ref space, span<std::byte>,
                            uintptr_t &out_node_addr) noexcept {
    return context->root(space, container_addr, out_node_addr);
  }

  static bool get_left_node(value_ref<const context_type> context,
                            address_space_ref space, span<std::byte>,
                            uintptr_t node_addr,
                            uintptr_t &out_left_addr) noexcept {
    return context->left(space, node_addr, out_left_addr);
  }

  static bool get_right_node(value_ref<const context_type> context,
                             address_space_ref space, span<std::byte>,
                             uintptr_t node_addr,
                             uintptr_t &out_right_addr) noexcept {
    return context->right(space, node_addr, out_right_addr);
  }

  static bool format_node(value_ref<const context_type> context,
                          address_space_ref space, span<std::byte> scratch,
                          uintptr_t node_addr,
                          const container_options &opts,
                          const sink &out) noexcept {
    if (opts.print_key) {
      const uintptr_t key_addr =
          detail::add_address_offset(node_addr, context->key_offset);
      if constexpr (remote_object_traits<Key>::is_registered) {
        remote_object_view view(key_addr, space, type_tag<Key>{}, scratch);
        formatter<remote_object_view>().format(view, out);
      } else {
        scratch_allocator allocator(scratch);
        Key *key = allocator.allocate<Key>();
        if (!key || !space.read_bytes(key_addr, key, sizeof(Key)))
          return false;
        formatter<Key>().format(*key, out);
      }
      out.write(opts.kv_separator);
    }

    if (opts.print_value) {
      const uintptr_t value_addr =
          detail::add_address_offset(node_addr, context->value_offset);
      if constexpr (remote_object_traits<Value>::is_registered) {
        remote_object_view view(value_addr, space, type_tag<Value>{}, scratch);
        formatter<remote_object_view>().format(view, out);
      } else {
        scratch_allocator allocator(scratch);
        Value *value = allocator.allocate<Value>();
        if (!value || !space.read_bytes(value_addr, value, sizeof(Value)))
          return false;
        formatter<Value>().format(*value, out);
      }
    }
    return true;
  }
};

/**
 * @brief Creates a binary-tree inspector for a conventional offset layout.
 */
template <typename Key, typename Value, typename RemotePtr = uintptr_t>
[[nodiscard]] constexpr auto
make_remote_binary_tree(uintptr_t container_addr, ptrdiff_t root_offset,
                        ptrdiff_t left_offset, ptrdiff_t right_offset,
                        ptrdiff_t key_offset,
                        ptrdiff_t value_offset) noexcept {
  using tag = detail::binary_tree_layout_tag<Key, Value, RemotePtr>;
  using traits = remote_binary_tree_traits<tag>;
  typename traits::context_type context{
      make_remote_offset_query<uintptr_t, RemotePtr>(root_offset),
      make_remote_offset_query<uintptr_t, RemotePtr>(left_offset),
      make_remote_offset_query<uintptr_t, RemotePtr>(right_offset),
      key_offset,
      value_offset};
  return remote_binary_tree<tag>(container_addr, std::move(context));
}

} // namespace microfmt
